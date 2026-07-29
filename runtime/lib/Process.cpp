//===- Process.cpp - Shared native/bytecode process instances ------------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <tuple>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace {

constexpr uint64_t kNativeLogicalProcessTag =
    OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;

std::mutex nativeScheduleRegistryMutex;
std::unordered_set<const void *> installedNativeScheduleStates;

// Marks calls made from an installed native AOT kernel. This selects AOT-only
// leaf operations, but does not waive the mutex contract of public runtime
// entry points: observers may temporarily release that mutex while callbacks
// run.
thread_local obelisk_rt_context *activeNativeAOTContext = nullptr;
// A static AOT frame may retain the real context mutex across the generated
// kernel. Nested runtime entry points then inherit that exclusion instead of
// repeatedly acquiring the same recursive mutex.
thread_local obelisk_rt_context *lockedNativeAOTContext = nullptr;

class ContextMutexLock {
public:
  explicit ContextMutexLock(obelisk_rt_context *context) {
    if (lockedNativeAOTContext != context)
      lock = std::unique_lock<std::recursive_mutex>(context->mutex);
  }

private:
  std::unique_lock<std::recursive_mutex> lock;
};

class NativeAOTMutexScope {
public:
  explicit NativeAOTMutexScope(obelisk_rt_context *context)
      : previous(lockedNativeAOTContext), lock(context->mutex) {
    lockedNativeAOTContext = context;
  }
  NativeAOTMutexScope(const NativeAOTMutexScope &) = delete;
  NativeAOTMutexScope &operator=(const NativeAOTMutexScope &) = delete;
  ~NativeAOTMutexScope() { lockedNativeAOTContext = previous; }

private:
  obelisk_rt_context *previous;
  std::unique_lock<std::recursive_mutex> lock;
};

class NativeAOTContextScope {
public:
  explicit NativeAOTContextScope(obelisk_rt_context *context)
      : previous(activeNativeAOTContext) {
    activeNativeAOTContext = context;
  }
  NativeAOTContextScope(const NativeAOTContextScope &) = delete;
  NativeAOTContextScope &operator=(const NativeAOTContextScope &) = delete;
  ~NativeAOTContextScope() { activeNativeAOTContext = previous; }

private:
  obelisk_rt_context *previous;
};

bool isStaticControlAOT(const obelisk_rt_context *context) {
  return activeNativeAOTContext == context && context->nativeSchedulePlan &&
         (context->nativeSchedulePlan->flags &
          OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0;
}

constexpr uint64_t kWaitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(obelisk_rt_wait_entry_v1);
constexpr uint64_t kProcessAllocationMagic = UINT64_C(0x4f42454c4652414d);

struct ProcessAllocationMetadata {
  uint64_t magic;
  size_t size;
  size_t alignment;
  obelisk_rt_context *managedRootContext = nullptr;
};

struct ProcessFreeBlock {
  ProcessFreeBlock *next;
};

constexpr size_t kProcessAllocationMetadataOffset =
    (sizeof(obelisk_rt_process_instance_v1) +
     alignof(ProcessAllocationMetadata) - 1) &
    ~(alignof(ProcessAllocationMetadata) - 1);

ProcessAllocationMetadata *
processMetadata(obelisk_rt_process_instance_v1 *instance) {
  return reinterpret_cast<ProcessAllocationMetadata *>(
      reinterpret_cast<uint8_t *>(instance) + kProcessAllocationMetadataOffset);
}

obelisk_rt_status
registerManagedFrameRoots(obelisk_rt_process_instance_v1 *instance,
                          obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_OK;
  ProcessAllocationMetadata *metadata = processMetadata(instance);
  if (metadata->managedRootContext)
    return metadata->managedRootContext == context
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_LIFECYCLE;

  const obelisk_rt_frame_layout_v1 &layout =
      *instance->descriptor->frame_layout;
  bool hasManagedRoots = false;
  for (uint32_t index = 0; index != layout.field_count; ++index)
    hasManagedRoots |=
        layout.fields[index].flags == OBELISK_RT_FRAME_MANAGED_ROOT;
  if (!hasManagedRoots) {
    metadata->managedRootContext = context;
    return OBELISK_RT_OK;
  }
  ContextMutexLock lock(context);
  if (std::find(context->managedRootProcesses.begin(),
                context->managedRootProcesses.end(),
                instance) != context->managedRootProcesses.end())
    return OBELISK_RT_INVALID_LIFECYCLE;
  context->managedRootProcesses.push_back(instance);
  metadata->managedRootContext = context;
  return OBELISK_RT_OK;
}

void unregisterManagedFrameRoots(obelisk_rt_process_instance_v1 *instance) {
  ProcessAllocationMetadata *metadata = processMetadata(instance);
  if (!metadata->managedRootContext)
    return;
  obelisk_rt_context *context = metadata->managedRootContext;
  ContextMutexLock lock(context);
  auto found = std::find(context->managedRootProcesses.begin(),
                         context->managedRootProcesses.end(), instance);
  if (found != context->managedRootProcesses.end())
    context->managedRootProcesses.erase(found);
  metadata->managedRootContext = nullptr;
}

constexpr unsigned kMinProcessSizeShift = 7;
constexpr unsigned kMaxProcessSizeShift = 20;
constexpr unsigned kMinProcessAlignmentShift = 4;
constexpr unsigned kMaxProcessAlignmentShift = 12;
constexpr size_t kProcessSizeClassCount =
    kMaxProcessSizeShift - kMinProcessSizeShift + 1;
constexpr size_t kProcessAlignmentClassCount =
    kMaxProcessAlignmentShift - kMinProcessAlignmentShift + 1;
constexpr size_t kProcessAllocationBucketCount =
    kProcessSizeClassCount * kProcessAlignmentClassCount;
constexpr size_t kMaxThreadLocalProcessFrameBytes = 64 * 1024;

struct ThreadProcessAllocationCache {
  std::array<ProcessFreeBlock *, kProcessAllocationBucketCount> blocks{};

  ~ThreadProcessAllocationCache() {
    // Keep TLS teardown independent of every global runtime object. The
    // process-wide pool is intentionally immortal, and local blocks only need
    // their trivial node lifetime ended before returning storage to libc.
    for (ProcessFreeBlock *allocation : blocks)
      if (allocation) {
        allocation->~ProcessFreeBlock();
        std::free(allocation);
      }
  }
};

thread_local ThreadProcessAllocationCache threadProcessAllocationCache;

// Persistent worker lanes normally recycle their hottest small frame class
// without synchronization. Overflow and cross-lane reclamation use independent
// size/alignment buckets, so unrelated frame classes never share one lock.
class ProcessAllocationPool {
public:
  void *allocate(size_t size, size_t alignment) noexcept {
    size_t classSize = 0;
    std::optional<size_t> bucketIndex = classify(size, alignment, classSize);
    if (!bucketIndex)
      return std::aligned_alloc(alignment, size);

    if (classSize <= kMaxThreadLocalProcessFrameBytes) {
      ProcessFreeBlock *&local =
          threadProcessAllocationCache.blocks[*bucketIndex];
      if (local) {
        ProcessFreeBlock *allocation = local;
        local = nullptr;
        allocation->~ProcessFreeBlock();
        return allocation;
      }
    }

    {
      Bucket &bucket = buckets[*bucketIndex];
      std::lock_guard<std::mutex> lock(bucket.mutex);
      if (bucket.head) {
        ProcessFreeBlock *allocation = bucket.head;
        bucket.head = allocation->next;
        --bucket.count;
        cachedBytes.fetch_sub(classSize, std::memory_order_relaxed);
        allocation->~ProcessFreeBlock();
        return allocation;
      }
    }
    return std::aligned_alloc(alignment, classSize);
  }

  void release(void *allocation, size_t size, size_t alignment) noexcept {
    constexpr size_t maxBlocksPerClass = 256;
    constexpr size_t maxCachedBytes = 32 * 1024 * 1024;
    if (!allocation)
      return;

    size_t classSize = 0;
    std::optional<size_t> bucketIndex = classify(size, alignment, classSize);
    if (!bucketIndex) {
      std::free(allocation);
      return;
    }

    if (classSize <= kMaxThreadLocalProcessFrameBytes) {
      ProcessFreeBlock *&local =
          threadProcessAllocationCache.blocks[*bucketIndex];
      if (!local) {
        local = ::new (allocation) ProcessFreeBlock{nullptr};
        return;
      }
    }

    size_t cached = cachedBytes.load(std::memory_order_relaxed);
    for (;;) {
      if (classSize > maxCachedBytes - cached) {
        std::free(allocation);
        return;
      }
      if (cachedBytes.compare_exchange_weak(cached, cached + classSize,
                                            std::memory_order_relaxed))
        break;
    }

    Bucket &bucket = buckets[*bucketIndex];
    {
      std::lock_guard<std::mutex> lock(bucket.mutex);
      if (bucket.count >= maxBlocksPerClass) {
        cachedBytes.fetch_sub(classSize, std::memory_order_relaxed);
        std::free(allocation);
        return;
      }
      bucket.head = ::new (allocation) ProcessFreeBlock{bucket.head};
      ++bucket.count;
    }
  }

private:
  struct alignas(64) Bucket {
    std::mutex mutex;
    ProcessFreeBlock *head = nullptr;
    size_t count = 0;
  };

  static std::optional<size_t> classify(size_t size, size_t alignment,
                                        size_t &classSize) {
    unsigned sizeShift = kMinProcessSizeShift;
    classSize = size_t{1} << sizeShift;
    while (classSize < size && sizeShift < kMaxProcessSizeShift) {
      ++sizeShift;
      classSize <<= 1;
    }
    if (classSize < size ||
        alignment < (size_t{1} << kMinProcessAlignmentShift) ||
        alignment > (size_t{1} << kMaxProcessAlignmentShift) ||
        (alignment & (alignment - 1)) != 0)
      return std::nullopt;
    unsigned alignmentShift = kMinProcessAlignmentShift;
    while ((size_t{1} << alignmentShift) < alignment)
      ++alignmentShift;
    return (sizeShift - kMinProcessSizeShift) * kProcessAlignmentClassCount +
           alignmentShift - kMinProcessAlignmentShift;
  }

  std::array<Bucket, kProcessAllocationBucketCount> buckets;
  std::atomic<size_t> cachedBytes{0};
};

ProcessAllocationPool *processAllocationPool() noexcept {
  // Process instances may exist without a simulation context, so the pool has
  // process lifetime. Keep it intentionally immortal: this avoids static
  // destructor ordering against worker TLS teardown while the bounded cache
  // prevents unbounded retention.
  static ProcessAllocationPool *const pool =
      new (std::nothrow) ProcessAllocationPool;
  return pool;
}

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

} // namespace

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

namespace {

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
  auto validSignalHandle = [](uint64_t stableID) {
    obelisk_rt_stable_handle_v1 decoded;
    return obelisk_rt_stable_handle_decode(stableID, &decoded);
  };
  auto entriesMatch = [&](bool requireEdge, obelisk_rt_wait_edge_kind exactEdge,
                          bool requireSignalHandle = false) {
    for (uint32_t index = 0; index != wait->count; ++index) {
      const obelisk_rt_wait_entry_v1 &entry = entries[index];
      if (requireSignalHandle && !validSignalHandle(entry.stable_id))
        return false;
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
    valid = (wait->flags == 0 || wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE) &&
            wait->count == 1 && wait->payload == 0 && wait->auxiliary == 0 &&
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_CHANGE, true);
    break;
  case OBELISK_RT_SUSPEND_EDGE:
    if (wait->flags == OBELISK_RT_WAIT_EDGE_IFF)
      valid = wait->count == 2 && wait->payload == 0 && wait->auxiliary == 0 &&
              validEdge(entries[0].edge) && entries[0].reserved != 0 &&
              entries[1].edge == OBELISK_RT_WAIT_EDGE_NONE &&
              entries[1].reserved != 0 &&
              validSignalHandle(entries[0].stable_id) &&
              validSignalHandle(entries[1].stable_id);
    else
      valid = wait->flags == 0 && wait->count == 1 && wait->payload == 0 &&
              wait->auxiliary == 0 &&
              entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE, true);
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
            entriesMatch(true, OBELISK_RT_WAIT_EDGE_NONE, true);
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
    if (action.flags != 0 || action.suspend_kind != OBELISK_RT_SUSPEND_NONE ||
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

const NativeStaticState *
findNativeStaticState(const obelisk_rt_context *context, uint32_t id) {
  if (id < context->nativeScheduleStaticStateIndex.size() &&
      context->nativeScheduleStaticStateIndex[id].bitWidth != 0)
    return &context->nativeScheduleStaticStateIndex[id];
  auto found = context->nativeStaticStates.find(id);
  return found == context->nativeStaticStates.end() ? nullptr : &found->second;
}

obelisk_rt_status resolveCheckedNativePackedRangeUnlocked(
    obelisk_rt_context *context, uint64_t handle, uint64_t globalBitCount,
    uint64_t &rootOffset, uint64_t &rootWidth, int64_t &coordinate) {
  rootOffset = 0;
  rootWidth = globalBitCount;
  uint32_t staticID = 0;
  if (decodeNativeStatic(handle, staticID, coordinate)) {
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state || state->bitOffset > globalBitCount ||
        state->bitWidth > globalBitCount - state->bitOffset)
      return OBELISK_RT_INVALID_HANDLE;
    rootOffset = state->bitOffset;
    rootWidth = state->bitWidth;
    return OBELISK_RT_OK;
  }
  return decodeNativeGlobal(handle, coordinate) ? OBELISK_RT_OK
                                                : OBELISK_RT_INVALID_HANDLE;
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
    if (!lhsStatic && (!decodeNativeGlobal(lhsHandle, lhs) ||
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
  ContextMutexLock lock(context);
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();) {
    if (state->second.owner != instance) {
      ++state;
      continue;
    }
    state->second.owner = nullptr;
    if (state->second.referenceCount <= 1) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
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

bool validNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                    const uint8_t *value,
                                    const uint8_t *unknown, uint64_t bitCount) {
  return context && value && unknown && context->execution &&
         context->execution->state_bit_count == bitCount &&
         context->stateValue.size() == (bitCount + 63) / 64 &&
         context->stateUnknown.size() == context->stateValue.size();
}

bool importNativeStatePlanesUnlocked(obelisk_rt_context *context,
                                     const uint8_t *value,
                                     const uint8_t *unknown,
                                     uint64_t bitCount) {
  if (!validNativeStatePlanesUnlocked(context, value, unknown, bitCount))
    return false;
  std::fill(context->stateValue.begin(), context->stateValue.end(), 0);
  std::fill(context->stateUnknown.begin(), context->stateUnknown.end(), 0);
  for (uint64_t bit = 0; bit != bitCount; ++bit) {
    uint64_t mask = uint64_t{1} << (bit % 64);
    if (byteBit(value, bit))
      context->stateValue[bit / 64] |= mask;
    if (byteBit(unknown, bit))
      context->stateUnknown[bit / 64] |= mask;
  }
  return true;
}

bool exportNativeStatePlanesUnlocked(const obelisk_rt_context *context,
                                     uint8_t *value, uint8_t *unknown,
                                     uint64_t bitCount) {
  if (!validNativeStatePlanesUnlocked(context, value, unknown, bitCount))
    return false;
  size_t byteCount = static_cast<size_t>((bitCount + 7) / 8);
  std::fill_n(value, byteCount, uint8_t{0});
  std::fill_n(unknown, byteCount, uint8_t{0});
  for (uint64_t bit = 0; bit != bitCount; ++bit) {
    uint64_t mask = uint64_t{1} << (bit % 64);
    setByteBit(value, bit, (context->stateValue[bit / 64] & mask) != 0);
    setByteBit(unknown, bit, (context->stateUnknown[bit / 64] & mask) != 0);
  }
  return true;
}

bool reconcileNativeRootToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan, uint32_t id) {
  if (!context || !plan ||
      !validNativeStatePlanesUnlocked(context, plan->state_value,
                                      plan->state_unknown,
                                      plan->state_bit_count))
    return false;
  const NativeStaticState *state = findNativeStaticState(context, id);
  if (!state || state->bitOffset > plan->state_bit_count ||
      state->bitWidth > plan->state_bit_count - state->bitOffset)
    return false;
  for (uint64_t local = 0; local != state->bitWidth; ++local) {
    uint64_t absolute = state->bitOffset + local;
    uint64_t mask = uint64_t{1} << (absolute % 64);
    setByteBit(plan->state_value, absolute,
               (context->stateValue[absolute / 64] & mask) != 0);
    setByteBit(plan->state_unknown, absolute,
               (context->stateUnknown[absolute / 64] & mask) != 0);
  }
  return true;
}

bool reconcileNativeDirtyRootsToPlanesUnlocked(
    const obelisk_rt_context *context,
    const obelisk_rt_native_schedule_plan *plan) {
  if (!context || !plan)
    return false;
  for (uint32_t id : context->nativeScheduleTransientDirtyRoots)
    if (!reconcileNativeRootToPlanesUnlocked(context, plan, id))
      return false;
  for (uint32_t id : context->nativeSchedulePersistentDirtyRoots)
    if (!reconcileNativeRootToPlanesUnlocked(context, plan, id))
      return false;
  return true;
}

bool nativeMaskIntersectsRange(const std::vector<uint64_t> &mask,
                               uint64_t bitOffset, uint64_t bitWidth) {
  if (bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return false;
  uint64_t end = bitOffset + bitWidth;
  uint64_t firstLimb = bitOffset / 64;
  uint64_t lastLimb = (end - 1) / 64;
  if (firstLimb >= mask.size())
    return false;
  lastLimb = std::min<uint64_t>(lastLimb, mask.size() - 1);
  for (uint64_t limb = firstLimb; limb <= lastLimb; ++limb) {
    uint64_t selected = UINT64_MAX;
    if (limb == firstLimb)
      selected &= UINT64_MAX << (bitOffset % 64);
    if (limb == (end - 1) / 64 && end % 64 != 0)
      selected &= (uint64_t{1} << (end % 64)) - 1;
    if ((mask[limb] & selected) != 0)
      return true;
  }
  return false;
}

uint64_t packedWidthMask(uint64_t bitWidth) {
  return bitWidth == 64 ? UINT64_MAX : (uint64_t{1} << bitWidth) - 1;
}

uint64_t loadPackedBits(const std::vector<uint64_t> &plane, uint64_t bitOffset,
                        uint64_t bitWidth) {
  uint64_t limb = bitOffset / 64;
  uint32_t shift = bitOffset % 64;
  uint64_t value = plane[limb] >> shift;
  if (shift != 0 && bitWidth > 64 - shift)
    value |= plane[limb + 1] << (64 - shift);
  return value & packedWidthMask(bitWidth);
}

uint64_t loadPackedBytes(const uint8_t *plane, uint64_t bitOffset,
                         uint64_t bitWidth) {
  uint64_t firstByte = bitOffset / 8;
  uint32_t shift = bitOffset % 8;
  uint64_t byteCount = (shift + bitWidth + 7) / 8;
  unsigned __int128 bits = 0;
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    bits |= static_cast<unsigned __int128>(plane[firstByte + byte])
            << (byte * 8);
  return static_cast<uint64_t>(bits >> shift) & packedWidthMask(bitWidth);
}

void storePackedBits(std::vector<uint64_t> &plane, uint64_t bitOffset,
                     uint64_t bitWidth, uint64_t value) {
  uint64_t limb = bitOffset / 64;
  uint32_t shift = bitOffset % 64;
  uint64_t mask = packedWidthMask(bitWidth);
  value &= mask;
  uint64_t lowMask = mask << shift;
  plane[limb] = (plane[limb] & ~lowMask) | (value << shift);
  if (shift != 0 && bitWidth > 64 - shift) {
    uint32_t lowBits = 64 - shift;
    uint64_t highMask = mask >> lowBits;
    plane[limb + 1] = (plane[limb + 1] & ~highMask) | (value >> lowBits);
  }
}

void storePackedBytes(uint8_t *plane, uint64_t bitOffset, uint64_t bitWidth,
                      uint64_t value) {
  uint64_t firstByte = bitOffset / 8;
  uint32_t shift = bitOffset % 8;
  uint64_t byteCount = (shift + bitWidth + 7) / 8;
  unsigned __int128 bits = 0;
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    bits |= static_cast<unsigned __int128>(plane[firstByte + byte])
            << (byte * 8);
  unsigned __int128 mask = (static_cast<unsigned __int128>(1) << bitWidth) - 1;
  unsigned __int128 positionedMask = mask << shift;
  bits = (bits & ~positionedMask) |
         ((static_cast<unsigned __int128>(value) & mask) << shift);
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    plane[firstByte + byte] = static_cast<uint8_t>(bits >> (byte * 8));
}

bool nativeWaitReady(obelisk_rt_context &context,
                     const ScheduledProcess &process) {
  if (context.signalDiagnosticsEnabled)
    ++context.signalDiagnostics.readinessCalls;
  const obelisk_rt_wait_record_v1 *wait = currentWait(process);
  if (!wait)
    return false;
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  switch (process.suspendKind) {
  case OBELISK_RT_SUSPEND_OBSERVER:
    return process.signalTriggered;
  case OBELISK_RT_SUSPEND_CHANGE:
  case OBELISK_RT_SUSPEND_EDGE:
    return process.signalTriggered ||
           (process.signalLatch && process.signalLatch->triggered);
  case OBELISK_RT_SUSPEND_EVENT:
    if (process.waitGenerations.size() != wait->count)
      return false;
    for (uint32_t index = 0; index != wait->count; ++index) {
      auto found = context.events.find(entries[index].stable_id);
      uint64_t generation =
          found == context.events.end() ? 0 : found->second.generation;
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
      bool terminated = context.terminatedNativeProcesses.count(
                            entries[index].stable_id) != 0;
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

bool nativeProcessReady(obelisk_rt_context &context,
                        const ScheduledProcess &process,
                        bool directStaticSignalWait) {
  if (!process.started || process.suspendKind == OBELISK_RT_SUSPEND_NONE)
    return true;
  if (process.suspendKind == OBELISK_RT_SUSPEND_DELAY)
    return process.wakeTime <= context.schedulerTime;
  if (directStaticSignalWait &&
      (process.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
       process.suspendKind == OBELISK_RT_SUSPEND_EDGE))
    return process.signalTriggered ||
           (process.signalLatch && process.signalLatch->triggered);
  return nativeWaitReady(context, process);
}

bool indexedSignalBlocked(const ScheduledProcess &process) {
  if (!process.instance || !process.started || process.signalTriggered)
    return false;
  bool signalSuspend = process.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
                       process.suspendKind == OBELISK_RT_SUSPEND_EDGE;
  if (signalSuspend) {
    const obelisk_rt_wait_record_v1 *wait = currentWait(process);
    if (wait && (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
                 wait->flags == OBELISK_RT_WAIT_EDGE_IFF))
      return true;
  }
  return !process.signalSubscriptions.empty() && process.signalLatch &&
         !process.signalLatch->triggered &&
         (signalSuspend || process.suspendKind == OBELISK_RT_SUSPEND_OBSERVER);
}

void rebuildNativeSchedulerIndexUnlocked(obelisk_rt_context *context) {
  context->scheduledProcessIndices.clear();
  context->nativePollCandidates.clear();
  std::fill(context->nativeScheduleActorIndices.begin(),
            context->nativeScheduleActorIndices.end(), SIZE_MAX);
  context->scheduledProcessIndices.reserve(context->scheduledProcesses.size());
  context->nativePollCandidates.reserve(context->scheduledProcesses.size());
  for (size_t index = 0; index != context->scheduledProcesses.size(); ++index) {
    const ScheduledProcess &process = context->scheduledProcesses[index];
    context->scheduledProcessIndices[process.token] = index;
    if (process.aotActorSlot < context->nativeScheduleActorIndices.size())
      context->nativeScheduleActorIndices[process.aotActorSlot] = index;
    if (process.instance && !indexedSignalBlocked(process))
      context->nativePollCandidates.insert(process.token);
  }
}

} // namespace

void obelisk_rt_erase_automatic_bookkeeping_unlocked(
    obelisk_rt_context *context, uint32_t automaticID) {
  if (!context)
    return;
  if (auto state = context->nativeAutomaticStates.find(automaticID);
      state != context->nativeAutomaticStates.end() &&
      state->second.managedRootRegistered) {
    state->second.managedRootRegistered = false;
  }
  if (auto state = context->nativeAutomaticStates.find(automaticID);
      state != context->nativeAutomaticStates.end() &&
      !state->second.managedRootByteOffsets.empty()) {
    state->second.managedRootByteOffsets.clear();
  }
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();) {
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(snapshot->first, id, offset) && id == automaticID)
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
  }

  uint64_t first = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_AUTOMATIC, automaticID, 0);
  if (first == UINT64_MAX)
    return;
  uint64_t last = first | UINT32_MAX;
  context->events.erase(context->events.lower_bound(first),
                        context->events.upper_bound(last));
}

void obelisk_rt_invalidate_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth) {
  if (!context || bitWidth == 0)
    return;
  // Snapshots are stored per canonical stable bit handle. For the common
  // scalar and narrow-write cases, erase the affected handles directly
  // instead of walking every snapshot retained by unrelated signals. Retain
  // the range scan for wide invalidations so the cost is bounded by the
  // smaller of the publication width and the snapshot population.
  if (bitWidth <= context->signalValueSnapshots.size()) {
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (bit > static_cast<uint64_t>(INT64_MAX))
        break;
      uint64_t handle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (handle != UINT64_MAX)
        context->signalValueSnapshots.erase(handle);
    }
    return;
  }
  for (auto snapshot = context->signalValueSnapshots.begin();
       snapshot != context->signalValueSnapshots.end();)
    if (nativeRangesOverlap(snapshot->first, 1, bitOffset, bitWidth))
      snapshot = context->signalValueSnapshots.erase(snapshot);
    else
      ++snapshot;
}

ScheduledProcess *findScheduledProcess(obelisk_rt_context *context,
                                       uint64_t token) {
  if (auto indexed = context->scheduledProcessIndices.find(token);
      indexed != context->scheduledProcessIndices.end() &&
      indexed->second < context->scheduledProcesses.size()) {
    ScheduledProcess &process = context->scheduledProcesses[indexed->second];
    if (process.token == token)
      return &process;
  }
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.token == token)
      return &process;
  return nullptr;
}

void wakeMonitorProcessUnlocked(obelisk_rt_context *context,
                                uint64_t logicalToken) {
  if (!logicalToken)
    return;
  if ((logicalToken & kNativeLogicalProcessTag) != 0) {
    uint64_t token = logicalToken & ~kNativeLogicalProcessTag;
    if (ScheduledProcess *process = findScheduledProcess(context, token);
        process && process->instance) {
      obelisk_rt_unregister_signal_wait_unlocked(
          context, process->signalSubscriptions, process->token, false);
      process->suspendKind = OBELISK_RT_SUSPEND_NONE;
      process->signalTriggered = false;
      process->urgent = false;
      process->queuedRegion = OBELISK_RT_REGION_POSTPONED;
      context->nativePollCandidates.insert(token);
      if (++context->schedulerSelectionGeneration == 0)
        context->schedulerSelectionGeneration = 1;
    }
    return;
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    if (task.id != logicalToken || task.terminated)
      continue;
    obelisk_rt_unregister_signal_wait_unlocked(
        context, task.signalSubscriptions, task.id, true);
    task.suspendKind = OBELISK_RT_SUSPEND_NONE;
    task.signalTriggered = false;
    task.urgent = false;
    task.queuedRegion = OBELISK_RT_REGION_POSTPONED;
    context->designPollCandidates.insert(logicalToken);
    if (++context->schedulerSelectionGeneration == 0)
      context->schedulerSelectionGeneration = 1;
    return;
  }
}

obelisk_rt_computed_wait_record_v1 *computedWait(ScheduledProcess &process) {
  if (process.suspendKind != OBELISK_RT_SUSPEND_OBSERVER || !process.instance ||
      !process.instance->frame ||
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

bool evaluateNativeObserver(obelisk_rt_context *context, uint64_t processToken,
                            obelisk_rt_computed_wait_record_v1 *wait,
                            uint32_t observerIndex,
                            std::vector<uint64_t> &value,
                            std::vector<uint64_t> &unknown) {
  if (!context || !wait || observerIndex >= wait->observer_count)
    return false;
  ScheduledProcess *process = findScheduledProcess(context, processToken);
  if (!process || !process->instance || !process->instance->descriptor)
    return false;
  auto *observers = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      reinterpret_cast<uint8_t *>(wait) + wait->observers_offset);
  auto *captures = reinterpret_cast<obelisk_rt_computed_capture_v1 *>(
      reinterpret_cast<uint8_t *>(wait) + wait->captures_offset);
  const obelisk_rt_computed_observer_v1 &binding = observers[observerIndex];
  const obelisk_rt_observer_descriptor_v1 *descriptor = findObserverDescriptor(
      process->instance->descriptor->execution, binding.code_unit_id);
  if (!descriptor || descriptor->capture_count != binding.capture_count)
    return false;
  if (context->observerDepth >= 256) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  value.assign(limbs, 0);
  unknown.assign(limbs, 0);
  std::vector<uint64_t> nativeCaptures(binding.capture_count);
  for (uint32_t index = 0; index != binding.capture_count; ++index)
    nativeCaptures[index] = captures[binding.capture_begin + index].stable_id;

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
    status = obelisk_rt_v1_native_state_retain(context, nativeCaptures[index]);
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
  std::vector<uint64_t> producerControls = std::move(context->activeControls);
  std::vector<uint64_t> waiterControls = process->controls;
  context->activeNativeProcess = waiter;
  context->activeLogicalProcessToken = kNativeLogicalProcessTag | processToken;
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
            context, nativeCaptures.data(), binding.capture_count, value.data(),
            unknown.data(), limbs);
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
  if (ScheduledProcess *updated = findScheduledProcess(context, processToken);
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
    uint64_t mask = (uint64_t{1} << (descriptor->result_width % 64)) - 1;
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
  if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
    tokens.swap(context->pendingNativeComputedWaiters);
  else {
    tokens.reserve(context->scheduledProcesses.size());
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance && process.started &&
          process.suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
          !process.signalTriggered)
        tokens.push_back(process.token);
  }

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
    auto primaryAffected = [&](const obelisk_rt_computed_observer_v1 &primary) {
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const obelisk_rt_computed_dependency_v1 &dependency =
            dependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind != dependencyKind)
          continue;
        if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT
                ? dependency.stable_id == publishedHandle
                : nativeRangesOverlap(dependency.stable_id, dependency.width,
                                      publishedHandle, publishedWidth))
          return true;
      }
      return false;
    };
    if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL) {
      bool affected = false;
      for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
           ++clauseIndex) {
        if (primaryAffected(observers[clauses[clauseIndex].primary_observer])) {
          affected = true;
          break;
        }
      }
      if (!affected) {
        try {
          context->pendingNativeComputedWaiters.push_back(token);
        } catch (const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
        continue;
      }
      if (process->signalLatch)
        process->signalLatch->affected = false;
    }
    for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
         ++clauseIndex) {
      obelisk_rt_computed_clause_v1 clause = clauses[clauseIndex];
      obelisk_rt_computed_observer_v1 primary =
          observers[clause.primary_observer];
      if (!primaryAffected(primary))
        continue;
      std::vector<uint64_t> value;
      std::vector<uint64_t> unknown;
      if (!evaluateNativeObserver(context, token, wait, clause.primary_observer,
                                  value, unknown))
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
      uint32_t limbs =
          static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
      auto *previousValue = reinterpret_cast<uint64_t *>(
          reinterpret_cast<uint8_t *>(wait) + primary.previous_offset);
      auto *previousUnknown = previousValue + limbs;
      bool changed = false;
      if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL32) != 0) {
        float previous = 0.0f;
        float current = 0.0f;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL64) != 0) {
        double previous = 0.0;
        double current = 0.0;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else {
        for (uint32_t limb = 0; limb != limbs; ++limb)
          changed |= previousValue[limb] != value[limb] ||
                     previousUnknown[limb] != unknown[limb];
      }
      uint32_t observedEdges = transitionEdges(
          (previousValue[0] & 1) != 0, (previousUnknown[0] & 1) != 0,
          (value[0] & 1) != 0, (unknown[0] & 1) != 0);
      for (uint32_t limb = 0; limb != limbs; ++limb) {
        previousValue[limb] = value[limb];
        previousUnknown[limb] = unknown[limb];
      }
      bool occurrence =
          (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           (clause.flags & OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0) ||
          (clause.edge == OBELISK_RT_WAIT_EDGE_CHANGE
               ? changed
               : signalEdgeMatches(clause.edge, observedEdges));
      if (!occurrence)
        continue;
      if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
        if (!evaluateNativeObserver(context, token, wait,
                                    clause.condition_observer, value, unknown))
          return false;
        process = findScheduledProcess(context, token);
        wait = process ? computedWait(*process) : nullptr;
        if (!process || !process->instance || process->signalTriggered || !wait)
          break;
        occurrence = !value.empty() && (value[0] & 1) != 0 &&
                     (unknown.empty() || (unknown[0] & 1) == 0);
      }
      if (occurrence) {
        if (ScheduledProcess *updated = findScheduledProcess(context, token);
            updated && updated->instance) {
          updated->signalTriggered = true;
          try {
            context->nativePollCandidates.insert(token);
          } catch (const std::bad_alloc &) {
            context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
            return false;
          }
          if (++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
        }
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
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID, width) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID, width);
}

namespace {

constexpr int64_t kSignalSubscriptionPageBits = 256;
constexpr int64_t kWideSignalSubscriptionPage = INT64_MIN;
constexpr __int128 kMaximumIndexedSignalPages = 64;

bool signalSubscriptionBucketRange(uint64_t stableID, uint64_t bitWidth,
                                   uint32_t &kind, uint32_t &id,
                                   int64_t &firstPage, int64_t &lastPage) {
  if (bitWidth == 0)
    return false;
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(stableID, &decoded))
    return false;
  auto page = [](__int128 bit) {
    __int128 result = bit / kSignalSubscriptionPageBits;
    if (bit < 0 && bit % kSignalSubscriptionPageBits != 0)
      --result;
    return result;
  };
  __int128 first = page(decoded.offset);
  __int128 last = page(static_cast<__int128>(decoded.offset) + bitWidth - 1);
  if (first < INT64_MIN || first > INT64_MAX || last < INT64_MIN ||
      last > INT64_MAX)
    return false;
  kind = decoded.kind;
  id = decoded.id;
  firstPage = static_cast<int64_t>(first);
  lastPage = static_cast<int64_t>(last);
  return true;
}

bool appendSignalSubscriptionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edge, SignalSubscription::Target target, uint64_t waiterToken,
    SignalWaitLatch *latch,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions) {
  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  bool wide = pageCount > kMaximumIndexedSignalPages;

  auto subscription = std::make_unique<SignalSubscription>();
  subscription->stableID = stableID;
  subscription->bitWidth = bitWidth;
  subscription->edge = edge;
  subscription->target = target;
  subscription->waiterToken = waiterToken;
  subscription->latch = latch;
  subscription->bucketSlots.reserve(wide ? 1 : static_cast<size_t>(pageCount));
  subscriptions.push_back(std::move(subscription));
  SignalSubscription &stored = *subscriptions.back();
  if (context->signalDiagnosticsEnabled) {
    ++context->signalDiagnostics.subscriptionsCurrent;
    context->signalDiagnostics.subscriptionsHighWater =
        std::max(context->signalDiagnostics.subscriptionsHighWater,
                 context->signalDiagnostics.subscriptionsCurrent);
  }
  int64_t indexedFirst = wide ? kWideSignalSubscriptionPage : firstPage;
  int64_t indexedLast = wide ? kWideSignalSubscriptionPage : lastPage;
  for (int64_t page = indexedFirst;; ++page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    bool bucketEntryAppended = false;
    size_t slotIndex = stored.bucketSlots.size();
    try {
      auto &bucket = context->signalSubscriptionBuckets[key];
      bucket.push_back({&stored, slotIndex});
      bucketEntryAppended = true;
      stored.bucketSlots.push_back({key, bucket.size() - 1});
    } catch (...) {
      auto found = context->signalSubscriptionBuckets.find(key);
      if (found != context->signalSubscriptionBuckets.end()) {
        if (bucketEntryAppended && !found->second.empty() &&
            found->second.back().subscription == &stored &&
            found->second.back().slotIndex == slotIndex)
          found->second.pop_back();
        if (found->second.empty())
          context->signalSubscriptionBuckets.erase(found);
      }
      throw;
    }
    if (page == indexedLast)
      break;
  }
  return true;
}

} // namespace

void obelisk_rt_unregister_signal_wait_unlocked(
    obelisk_rt_context *context,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    uint64_t waiterToken, bool designWaiter) {
  if (!context) {
    subscriptions.clear();
    return;
  }
  if (waiterToken != 0) {
    if (designWaiter)
      context->designConditionalSignalWaiters.erase(waiterToken);
    else
      context->nativeConditionalSignalWaiters.erase(waiterToken);
  }
  for (const std::unique_ptr<SignalSubscription> &owned : subscriptions) {
    if (!owned)
      continue;
    SignalSubscription &subscription = *owned;
    for (const SignalSubscriptionBucketSlot &slot : subscription.bucketSlots) {
      auto bucket = context->signalSubscriptionBuckets.find(slot.key);
      if (bucket == context->signalSubscriptionBuckets.end())
        continue;
      if (slot.bucketIndex < bucket->second.size()) {
        SignalSubscriptionBucketEntry &entry = bucket->second[slot.bucketIndex];
        if (entry.subscription == &subscription) {
          SignalSubscriptionBucketEntry moved = bucket->second.back();
          entry = moved;
          bucket->second.pop_back();
          if (moved.subscription && moved.subscription != &subscription &&
              moved.slotIndex < moved.subscription->bucketSlots.size())
            moved.subscription->bucketSlots[moved.slotIndex].bucketIndex =
                slot.bucketIndex;
        }
      }
      if (bucket->second.empty())
        context->signalSubscriptionBuckets.erase(bucket);
    }
    if (context->signalDiagnosticsEnabled &&
        context->signalDiagnostics.subscriptionsCurrent != 0)
      --context->signalDiagnostics.subscriptionsCurrent;
  }
  subscriptions.clear();
}

bool obelisk_rt_register_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch, uint64_t waiterToken,
    bool designWaiter) {
  if (!context || !wait)
    return false;
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  if (latch) {
    latch->triggered = false;
    latch->affected = false;
  }
  if (wait->kind != OBELISK_RT_SUSPEND_CHANGE &&
      wait->kind != OBELISK_RT_SUSPEND_EDGE)
    return true;
  if (wait->flags != OBELISK_RT_WAIT_FLAGS_NONE) {
    if ((wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
         wait->flags == OBELISK_RT_WAIT_EDGE_IFF) &&
        waiterToken != 0) {
      try {
        if (designWaiter)
          context->designConditionalSignalWaiters.insert(waiterToken);
        else
          context->nativeConditionalSignalWaiters.insert(waiterToken);
      } catch (const std::bad_alloc &) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
        return false;
      }
    }
    return true;
  }
  try {
    if (!latch)
      latch = std::make_unique<SignalWaitLatch>();
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return false;
  }
  const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
  try {
    subscriptions.reserve(wait->count);
    for (uint32_t index = 0; index != wait->count; ++index) {
      if (!appendSignalSubscriptionUnlocked(
              context, entries[index].stable_id, entries[index].reserved,
              entries[index].edge,
              designWaiter ? SignalSubscription::DesignDirectWait
                           : SignalSubscription::NativeDirectWait,
              waiterToken, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    return true;
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}

bool obelisk_rt_register_computed_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_computed_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch) {
  if (!context || !wait || waiterToken == 0)
    return false;
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  try {
    if (!latch)
      latch = std::make_unique<SignalWaitLatch>();
    latch->triggered = false;
    latch->affected = false;
    const auto *dependencies =
        reinterpret_cast<const obelisk_rt_computed_dependency_v1 *>(
            reinterpret_cast<const uint8_t *>(wait) +
            wait->dependencies_offset);
    subscriptions.reserve(wait->dependency_count);
    for (uint32_t index = 0; index != wait->dependency_count; ++index) {
      const obelisk_rt_computed_dependency_v1 &dependency = dependencies[index];
      if (dependency.kind != OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
        continue;
      if (!appendSignalSubscriptionUnlocked(
              context, dependency.stable_id, dependency.width,
              OBELISK_RT_WAIT_EDGE_NONE,
              designWaiter ? SignalSubscription::DesignComputedWait
                           : SignalSubscription::NativeComputedWait,
              waiterToken, latch.get(), subscriptions)) {
        obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                                   waiterToken, designWaiter);
        return false;
      }
    }
    return true;
  } catch (const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions,
                                             waiterToken, designWaiter);
  return false;
}

namespace {

bool markNativeAOTActorReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot);

template <typename Matches>
bool publishSignalOccurrenceUnlocked(obelisk_rt_context *context,
                                     uint64_t stableID, uint64_t bitWidth,
                                     Matches &&matches,
                                     uint64_t *outSequence = nullptr) {
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  if (outSequence)
    *outSequence = sequence;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  bool fullyStaticAOT = context->nativeSchedulePlan &&
                        !context->nativeScheduleDeoptimized &&
                        (context->nativeSchedulePlan->flags &
                         OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0;

  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  auto visitBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = context->signalSubscriptionBuckets.find(key);
    if (bucket != context->signalSubscriptionBuckets.end()) {
      for (const SignalSubscriptionBucketEntry &entry : bucket->second) {
        SignalSubscription *subscription = entry.subscription;
        if (!subscription || subscription->lastExaminedSequence == sequence)
          continue;
        subscription->lastExaminedSequence = sequence;
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.subscribersExamined;
        bool overlaps = false;
        if (fullyStaticAOT &&
            subscription->target == SignalSubscription::NativeDirectWait) {
          int64_t publishedOffset = static_cast<int32_t>(stableID);
          int64_t subscribedOffset =
              static_cast<int32_t>(subscription->stableID);
          overlaps =
              static_cast<__int128>(publishedOffset) + bitWidth >
                  subscribedOffset &&
              static_cast<__int128>(subscribedOffset) + subscription->bitWidth >
                  publishedOffset;
        } else {
          overlaps =
              nativeRangesOverlap(subscription->stableID,
                                  subscription->bitWidth, stableID, bitWidth);
        }
        if (!overlaps)
          continue;
        if (!matches(*subscription))
          continue;
        if (subscription->target == SignalSubscription::NativeDirectWait ||
            subscription->target == SignalSubscription::DesignDirectWait) {
          if (!subscription->latch || subscription->latch->triggered)
            continue;
          subscription->latch->triggered = true;
          bool staticallyPolledNative =
              fullyStaticAOT &&
              subscription->target == SignalSubscription::NativeDirectWait;
          if (staticallyPolledNative) {
            ScheduledProcess *scheduled =
                findScheduledProcess(context, subscription->waiterToken);
            if (!scheduled || scheduled->aotActorSlot == UINT32_MAX ||
                !markNativeAOTActorReadyUnlocked(context,
                                                 scheduled->aotActorSlot)) {
              context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
              return false;
            }
          }
          if (!staticallyPolledNative && subscription->waiterToken != 0) {
            auto &candidates =
                subscription->target == SignalSubscription::NativeDirectWait
                    ? context->nativePollCandidates
                    : context->designPollCandidates;
            try {
              candidates.insert(subscription->waiterToken);
            } catch (const std::bad_alloc &) {
              context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
              return false;
            }
          }
          if (!staticallyPolledNative &&
              ++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
          continue;
        }
        if (!subscription->latch || subscription->latch->affected)
          continue;
        try {
          auto &pending =
              subscription->target == SignalSubscription::NativeComputedWait
                  ? context->pendingNativeComputedWaiters
                  : context->pendingDesignComputedWaiters;
          pending.push_back(subscription->waiterToken);
          subscription->latch->affected = true;
        } catch (const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
      }
    }
    return true;
  };
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= kMaximumIndexedSignalPages) {
    if (!visitBucket(kWideSignalSubscriptionPage))
      return false;
    for (int64_t page = firstPage;; ++page) {
      if (!visitBucket(page))
        return false;
      if (page == lastPage)
        break;
    }
  } else {
    std::vector<int64_t> pages;
    try {
      for (const auto &[key, bucket] : context->signalSubscriptionBuckets) {
        (void)bucket;
        if (key.kind == kind && key.id == objectID)
          pages.push_back(key.page);
      }
    } catch (const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    for (int64_t page : pages)
      if (!visitBucket(page))
        return false;
  }
  return true;
}

bool signalTransitionBatchMatches(const SignalSubscription &subscription,
                                  uint64_t stableID, uint64_t bitWidth,
                                  const uint8_t *changed,
                                  const uint8_t *posedge,
                                  const uint8_t *negedge,
                                  uint64_t edgeBitOffset) {
  int64_t publishedOffset = 0;
  int64_t subscribedOffset = 0;
  if ((stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG &&
      (subscription.stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    if ((stableID >> 32) != (subscription.stableID >> 32))
      return false;
    publishedOffset = static_cast<int32_t>(stableID);
    subscribedOffset = static_cast<int32_t>(subscription.stableID);
  } else {
    obelisk_rt_stable_handle_v1 published;
    obelisk_rt_stable_handle_v1 subscribed;
    if (!obelisk_rt_stable_handle_decode(stableID, &published) ||
        !obelisk_rt_stable_handle_decode(subscription.stableID, &subscribed) ||
        published.kind != subscribed.kind ||
        (published.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
         published.id != subscribed.id))
      return false;
    publishedOffset = published.offset;
    subscribedOffset = subscribed.offset;
  }
  __int128 overlapBegin = std::max<__int128>(publishedOffset, subscribedOffset);
  __int128 overlapEnd =
      std::min(static_cast<__int128>(publishedOffset) + bitWidth,
               static_cast<__int128>(subscribedOffset) + subscription.bitWidth);
  bool direct = subscription.target == SignalSubscription::NativeDirectWait ||
                subscription.target == SignalSubscription::DesignDirectWait;
  for (__int128 coordinate = overlapBegin; coordinate < overlapEnd;
       ++coordinate) {
    uint64_t index =
        edgeBitOffset + static_cast<uint64_t>(coordinate - publishedOffset);
    uint32_t observed = byteBit(changed, index) ? OBELISK_RT_SIGNAL_CHANGE : 0;
    if (byteBit(posedge, index))
      observed |= OBELISK_RT_SIGNAL_POSEDGE;
    if (byteBit(negedge, index))
      observed |= OBELISK_RT_SIGNAL_NEGEDGE;
    if (observed != 0 &&
        (!direct || signalEdgeMatches(subscription.edge, observed)))
      return true;
  }
  return false;
}

bool nativeStaticSpecializationEnvironmentClean(
    const obelisk_rt_context *context) {
  return context &&
         (!context->execution ||
          (((context->execution->flags &
             OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) == 0) &&
           context->execution->observer_count == 0)) &&
         context->scheduledDesignTasks.empty() &&
         context->nativeConditionalSignalWaiters.empty() &&
         context->designConditionalSignalWaiters.empty();
}

bool canUseStaticAOTFanout(const obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan ||
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT) == 0 &&
       !context->nativeScheduleGuardedFanoutActive) ||
      context->nativeScheduleDeoptimized || !context->execution)
    return false;
  // The activation guard's fast flag already represents this same clean
  // environment and is invalidated synchronously on every writable-VPI
  // handover. Reuse it instead of rescanning empty runtime inventories at
  // every NBA root commit.
  return (plan->specialization_fast && *plan->specialization_fast != 0) ||
         nativeStaticSpecializationEnvironmentClean(context);
}

bool staticNBARootNeedsTransitions(const obelisk_rt_context *context,
                                   uint32_t rootIndex) {
  return !canUseStaticAOTFanout(context) ||
         rootIndex >= context->staticNBARootHasFanout.size() ||
         context->staticNBARootHasFanout[rootIndex] != 0;
}

bool nativeStaticRootDirty(const obelisk_rt_context *context,
                           uint32_t staticState) {
  auto dirty = [staticState](const std::vector<uint8_t> &mask,
                             const std::unordered_set<uint32_t> &sparse) {
    return staticState < mask.size() ? mask[staticState] != 0
                                     : sparse.find(staticState) != sparse.end();
  };
  return context->nativeScheduleDirtyRootsPresent &&
         (dirty(context->nativeScheduleTransientDirtyMask,
                context->nativeScheduleTransientDirtyRoots) ||
          dirty(context->nativeSchedulePersistentDirtyMask,
                context->nativeSchedulePersistentDirtyRoots));
}

void invalidateNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan || !plan->specialization_fast)
    return;
  *plan->specialization_fast = 0;
}

void refreshNativeStaticSpecializationFastUnlocked(
    obelisk_rt_context *context) {
  const obelisk_rt_native_schedule_plan *plan =
      context ? context->nativeSchedulePlan : nullptr;
  if (!plan || !plan->specialization_fast || *plan->specialization_fast != 0)
    return;
  bool slowNBA = std::any_of(context->staticNBASlowRoots.begin(),
                             context->staticNBASlowRoots.end(),
                             [](uint8_t slow) { return slow != 0; });
  *plan->specialization_fast =
      context->nativeScheduleRunning && !context->nativeScheduleDeoptimized &&
              !context->nativeScheduleExternalWritePending &&
              !context->nativeScheduleDirtyRootsPresent && !slowNBA &&
              nativeStaticSpecializationEnvironmentClean(context)
          ? 1
          : 0;
}

bool nativeAOTActorDirty(const obelisk_rt_context *context,
                         uint32_t actorSlot) {
  if (context->nativeScheduleTransientDirtyRoots.empty() &&
      context->nativeSchedulePersistentDirtyRoots.empty())
    return true;
  bool described = false;
  for (uint64_t index = 0; index != context->nativeScheduleActorRootCount;
       ++index) {
    const obelisk_rt_static_actor_root &dependency =
        context->nativeScheduleActorRoots[index];
    if (dependency.actor_slot != actorSlot)
      continue;
    described = true;
    if (nativeStaticRootDirty(context, dependency.static_state))
      return true;
  }
  // Plans without dependency metadata retain the conservative handover.
  return !described;
}

bool nativeAOTNeedsSpecializationHandoverUnlocked(
    const obelisk_rt_context *context, uint32_t actorSlot) {
  bool dirtyActor = context->nativeScheduleDirtyRootsPresent &&
                    nativeAOTActorDirty(context, actorSlot);
  bool slowNBA = std::any_of(context->staticNBASlowRoots.begin(),
                             context->staticNBASlowRoots.end(),
                             [](uint8_t slow) { return slow != 0; });
  bool globalHandover = !context->nativeScheduleDirtyRootsPresent || slowNBA ||
                        !nativeStaticSpecializationEnvironmentClean(context);
  return dirtyActor || globalHandover;
}

uint32_t findNativeAOTNodeUnlocked(const obelisk_rt_context *context,
                                   uint32_t actorSlot, uint32_t continuation) {
  if (actorSlot >= context->nativeScheduleActorNodes.size())
    return UINT32_MAX;
  const auto &entries = context->nativeScheduleActorNodes[actorSlot];
  auto found =
      std::lower_bound(entries.begin(), entries.end(), continuation,
                       [](const std::pair<uint32_t, uint32_t> &entry,
                          uint32_t value) { return entry.first < value; });
  return found != entries.end() && found->first == continuation ? found->second
                                                                : UINT32_MAX;
}

bool markNativeAOTActorReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot) {
  if (context->nativeScheduleNodes.empty())
    return true;
  if (actorSlot >= context->nativeScheduleActors.size())
    return false;
  obelisk_rt_process_instance_v1 *actor =
      context->nativeScheduleActors[actorSlot];
  size_t processIndex = context->nativeScheduleActorIndices[actorSlot];
  if (!actor || processIndex >= context->scheduledProcesses.size())
    return false;
  const ScheduledProcess &scheduled = context->scheduledProcesses[processIndex];
  if (scheduled.instance != actor ||
      scheduled.phase != (context->schedulerRunningFinals ? 1u : 0u))
    return true;
  uint32_t node =
      findNativeAOTNodeUnlocked(context, actorSlot, actor->continuation);
  if (node == UINT32_MAX)
    return false;
  context->nativeScheduleReadyNodes[node / 64] |= uint64_t{1} << (node % 64);
  context->nativeScheduleMinimumActivatedNode =
      std::min(context->nativeScheduleMinimumActivatedNode, node);
  return true;
}

void clearNativeAOTNodeReadyUnlocked(obelisk_rt_context *context,
                                     uint32_t node) {
  if (node / 64 < context->nativeScheduleReadyNodes.size())
    context->nativeScheduleReadyNodes[node / 64] &=
        ~(uint64_t{1} << (node % 64));
}

bool nativeAOTDeadlineLess(const obelisk_rt_context *context, uint32_t lhs,
                           uint32_t rhs) {
  return std::pair{context->nativeScheduleDeadlines[lhs], lhs} <
         std::pair{context->nativeScheduleDeadlines[rhs], rhs};
}

void swapNativeAOTDeadlinesUnlocked(obelisk_rt_context *context, size_t lhs,
                                    size_t rhs) {
  std::swap(context->nativeScheduleDeadlineHeap[lhs],
            context->nativeScheduleDeadlineHeap[rhs]);
  context->nativeScheduleDeadlinePositions
      [context->nativeScheduleDeadlineHeap[lhs]] = lhs;
  context->nativeScheduleDeadlinePositions
      [context->nativeScheduleDeadlineHeap[rhs]] = rhs;
}

void siftNativeAOTDeadlineUpUnlocked(obelisk_rt_context *context,
                                     size_t position) {
  while (position != 0) {
    size_t parent = (position - 1) / 2;
    if (!nativeAOTDeadlineLess(context,
                               context->nativeScheduleDeadlineHeap[position],
                               context->nativeScheduleDeadlineHeap[parent]))
      break;
    swapNativeAOTDeadlinesUnlocked(context, position, parent);
    position = parent;
  }
}

void siftNativeAOTDeadlineDownUnlocked(obelisk_rt_context *context,
                                       size_t position) {
  size_t size = context->nativeScheduleDeadlineHeap.size();
  for (;;) {
    size_t child = position * 2 + 1;
    if (child >= size)
      return;
    if (child + 1 < size &&
        nativeAOTDeadlineLess(context,
                              context->nativeScheduleDeadlineHeap[child + 1],
                              context->nativeScheduleDeadlineHeap[child]))
      ++child;
    if (!nativeAOTDeadlineLess(context,
                               context->nativeScheduleDeadlineHeap[child],
                               context->nativeScheduleDeadlineHeap[position]))
      return;
    swapNativeAOTDeadlinesUnlocked(context, position, child);
    position = child;
  }
}

void removeNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                     uint32_t actorSlot) {
  if (actorSlot >= context->nativeScheduleDeadlinePositions.size())
    return;
  uint32_t position = context->nativeScheduleDeadlinePositions[actorSlot];
  if (position == UINT32_MAX)
    return;
  size_t last = context->nativeScheduleDeadlineHeap.size() - 1;
  if (position != last)
    swapNativeAOTDeadlinesUnlocked(context, position, last);
  context->nativeScheduleDeadlineHeap.pop_back();
  context->nativeScheduleDeadlinePositions[actorSlot] = UINT32_MAX;
  context->nativeScheduleDeadlines[actorSlot] = UINT64_MAX;
  if (position < context->nativeScheduleDeadlineHeap.size()) {
    if (position != 0 &&
        nativeAOTDeadlineLess(
            context, context->nativeScheduleDeadlineHeap[position],
            context->nativeScheduleDeadlineHeap[(position - 1) / 2]))
      siftNativeAOTDeadlineUpUnlocked(context, position);
    else
      siftNativeAOTDeadlineDownUnlocked(context, position);
  }
}

void setNativeAOTDeadlineUnlocked(obelisk_rt_context *context,
                                  uint32_t actorSlot, uint64_t deadline) {
  if (actorSlot < context->nativeScheduleDeadlinePositions.size() &&
      context->nativeScheduleDeadlinePositions[actorSlot] == UINT32_MAX &&
      context->nativeScheduleDeadlineHeap.empty()) {
    context->nativeScheduleDeadlines[actorSlot] = deadline;
    context->nativeScheduleDeadlinePositions[actorSlot] = 0;
    context->nativeScheduleDeadlineHeap.push_back(actorSlot);
    context->signalDiagnostics.aotDeadlineHighWater =
        std::max<uint64_t>(context->signalDiagnostics.aotDeadlineHighWater, 1);
    return;
  }
  removeNativeAOTDeadlineUnlocked(context, actorSlot);
  context->nativeScheduleDeadlines[actorSlot] = deadline;
  context->nativeScheduleDeadlinePositions[actorSlot] =
      context->nativeScheduleDeadlineHeap.size();
  context->nativeScheduleDeadlineHeap.push_back(actorSlot);
  siftNativeAOTDeadlineUpUnlocked(
      context, context->nativeScheduleDeadlineHeap.size() - 1);
  context->signalDiagnostics.aotDeadlineHighWater =
      std::max<uint64_t>(context->signalDiagnostics.aotDeadlineHighWater,
                         context->nativeScheduleDeadlineHeap.size());
}

bool markDueNativeAOTDeadlinesUnlocked(obelisk_rt_context *context) {
  if (context->nativeScheduleDeadlineHeap.size() == 1) {
    uint32_t slot = context->nativeScheduleDeadlineHeap.front();
    if (context->nativeScheduleDeadlines[slot] > context->schedulerTime)
      return true;
    context->nativeScheduleDeadlineHeap.clear();
    context->nativeScheduleDeadlinePositions[slot] = UINT32_MAX;
    context->nativeScheduleDeadlines[slot] = UINT64_MAX;
    return markNativeAOTActorReadyUnlocked(context, slot);
  }
  while (!context->nativeScheduleDeadlineHeap.empty()) {
    uint32_t slot = context->nativeScheduleDeadlineHeap.front();
    if (context->nativeScheduleDeadlines[slot] > context->schedulerTime)
      break;
    removeNativeAOTDeadlineUnlocked(context, slot);
    if (!markNativeAOTActorReadyUnlocked(context, slot))
      return false;
  }
  return true;
}

obelisk_rt_status
refreshNativeAOTReadyPhaseUnlocked(obelisk_rt_context *context) {
  std::fill(context->nativeScheduleReadyNodes.begin(),
            context->nativeScheduleReadyNodes.end(), 0);
  for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
       ++slot) {
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t processIndex = context->nativeScheduleActorIndices[slot];
    if (processIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[processIndex];
    if (scheduled.instance != actor)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (scheduled.phase != (context->schedulerRunningFinals ? 1u : 0u))
      continue;
    if (nativeProcessReady(*context, scheduled, true) &&
        !markNativeAOTActorReadyUnlocked(context, slot))
      return OBELISK_RT_INVALID_CONTINUATION;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
initializeNativeAOTNodesUnlocked(obelisk_rt_context *context,
                                 const obelisk_rt_native_schedule_node *nodes,
                                 uint32_t nodeCount) {
  if (!context->nativeScheduleNodes.empty()) {
    if (context->nativeScheduleNodes.size() != nodeCount ||
        !std::equal(context->nativeScheduleNodes.begin(),
                    context->nativeScheduleNodes.end(), nodes,
                    [](const obelisk_rt_native_schedule_node &lhs,
                       const obelisk_rt_native_schedule_node &rhs) {
                      return lhs.actor_slot == rhs.actor_slot &&
                             lhs.continuation == rhs.continuation &&
                             lhs.fusion_group == rhs.fusion_group;
                    }))
      return OBELISK_RT_INVALID_ARGUMENT;
    return OBELISK_RT_OK;
  }
  std::vector<obelisk_rt_native_schedule_node> installedNodes(
      nodes, nodes + nodeCount);
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> actorNodes(
      context->nativeScheduleActors.size());
  for (uint32_t nodeIndex = 0; nodeIndex != nodeCount; ++nodeIndex) {
    const obelisk_rt_native_schedule_node &node = nodes[nodeIndex];
    if (node.actor_slot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_ARGUMENT;
    actorNodes[node.actor_slot].emplace_back(node.continuation, nodeIndex);
  }
  for (auto &entries : actorNodes) {
    std::sort(entries.begin(), entries.end());
    for (size_t index = 1; index != entries.size(); ++index)
      if (entries[index - 1].first == entries[index].first)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  context->nativeScheduleNodes = std::move(installedNodes);
  context->nativeScheduleActorNodes = std::move(actorNodes);
  context->nativeScheduleReadyNodes.assign((uint64_t{nodeCount} + 63) / 64, 0);
  for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
       ++slot) {
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t processIndex = context->nativeScheduleActorIndices[slot];
    if (processIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[processIndex];
    if (scheduled.instance != actor)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (scheduled.started &&
        scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY) {
      setNativeAOTDeadlineUnlocked(context, slot, scheduled.wakeTime);
      continue;
    }
  }
  obelisk_rt_status status = refreshNativeAOTReadyPhaseUnlocked(context);
  if (status == OBELISK_RT_OK)
    return status;
  context->nativeScheduleNodes.clear();
  context->nativeScheduleActorNodes.clear();
  context->nativeScheduleReadyNodes.clear();
  context->nativeScheduleDeadlineHeap.clear();
  std::fill(context->nativeScheduleDeadlines.begin(),
            context->nativeScheduleDeadlines.end(), UINT64_MAX);
  std::fill(context->nativeScheduleDeadlinePositions.begin(),
            context->nativeScheduleDeadlinePositions.end(), UINT32_MAX);
  return status;
}

bool staticAOTFanoutRangeHasConsumer(const obelisk_rt_context *context,
                                     uint32_t staticID, uint64_t staticOffset,
                                     uint64_t bitWidth) {
  if (context->nativeScheduleFanoutEntryCount == 0)
    return false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first =
      std::lower_bound(begin, end, staticID,
                       [](const obelisk_rt_static_fanout_entry &entry,
                          uint32_t id) { return entry.static_state < id; });
  auto last = first;
  while (last != end && last->static_state == staticID)
    ++last;
  return std::any_of(
      first, last, [&](const obelisk_rt_static_fanout_entry &entry) {
        return static_cast<__int128>(staticOffset) <
                   static_cast<__int128>(entry.low_bit) + entry.bit_width &&
               static_cast<__int128>(entry.low_bit) <
                   static_cast<__int128>(staticOffset) + bitWidth;
      });
}

bool publishStaticAOTSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence) {
  if (outSequence)
    *outSequence = 0;
  if (!canUseStaticAOTFanout(context))
    return false;
  uint32_t staticID = 0;
  int64_t staticOffset = 0;
  if (!decodeNativeStatic(stableID, staticID, staticOffset) || staticOffset < 0)
    return false;
  uint64_t publishedLow = static_cast<uint64_t>(staticOffset);
  // Exact static fanout proves that no observer, conditional waiter, design
  // task, or unlisted direct wait can consume this transition.  Keep the
  // canonical state update but do not manufacture a scheduler publication.
  if (!staticAOTFanoutRangeHasConsumer(context, staticID, publishedLow,
                                       bitWidth))
    return true;
  bool published = false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first =
      std::lower_bound(begin, end, staticID,
                       [](const obelisk_rt_static_fanout_entry &entry,
                          uint32_t id) { return entry.static_state < id; });
  for (auto entry = first; entry != end && entry->static_state == staticID;
       ++entry) {
    ++context->signalDiagnostics.aotFanoutEntries;
    uint32_t slot = entry->actor_slot;
    if (slot >= context->nativeScheduleActors.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor || actor->continuation != entry->continuation)
      continue;
    size_t index = context->nativeScheduleActorIndices[slot];
    if (index >= context->scheduledProcesses.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    ScheduledProcess &scheduled = context->scheduledProcesses[index];
    if (scheduled.instance != actor || !scheduled.started ||
        scheduled.signalTriggered ||
        (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
         scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
      continue;
    uint64_t overlapLow = std::max(publishedLow, entry->low_bit);
    uint64_t overlapHigh =
        std::min(publishedLow + bitWidth, entry->low_bit + entry->bit_width);
    bool matched = false;
    for (uint64_t coordinate = overlapLow; coordinate < overlapHigh;
         ++coordinate) {
      uint64_t bit = coordinate - publishedLow;
      if (entry->edge == OBELISK_RT_WAIT_EDGE_CHANGE)
        matched = byteBit(changed, bit);
      else if (entry->edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
        matched = byteBit(posedge, bit);
      else if (entry->edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
        matched = byteBit(negedge, bit);
      else
        matched = byteBit(posedge, bit) || byteBit(negedge, bit);
      if (matched)
        break;
    }
    if (!matched)
      continue;
    if (!published) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return true;
      }
      uint64_t sequence = context->nextSchedulerSequence++;
      if (outSequence)
        *outSequence = sequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.publications;
      published = true;
    }
    scheduled.signalTriggered = true;
    if (!markNativeAOTActorReadyUnlocked(context, slot)) {
      context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
      return true;
    }
  }
  return true;
}

bool publishSignalTransitionBatchImpl(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence) {
  if (!context || bitWidth == 0 || !changed || !posedge || !negedge)
    return context != nullptr;
  bool anyChanged = false;
  for (uint64_t bit = 0; bit != bitWidth; ++bit)
    anyChanged |= byteBit(changed, edgeBitOffset + bit);
  if (!anyChanged)
    return true;
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        return signalTransitionBatchMatches(subscription, stableID, bitWidth,
                                            changed, posedge, negedge,
                                            edgeBitOffset);
      },
      outSequence);
}

} // namespace

bool obelisk_rt_publish_signal_transition_batch_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence) {
  if (edgeBitOffset == 0 && publishStaticAOTSignalTransitionUnlocked(
                                context, stableID, bitWidth, changed, posedge,
                                negedge, outSequence)) {
    return context->schedulerStatus == OBELISK_RT_OK;
  }
  return publishSignalTransitionBatchImpl(context, stableID, bitWidth, changed,
                                          posedge, negedge, edgeBitOffset,
                                          outSequence);
}

bool obelisk_rt_publish_signal_occurrence_unlocked(obelisk_rt_context *context,
                                                   uint64_t stableID,
                                                   uint64_t bitWidth,
                                                   uint32_t edges,
                                                   uint64_t *outSequence) {
  if (!context || bitWidth == 0 || edges == 0)
    return context != nullptr;
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        bool direct =
            subscription.target == SignalSubscription::NativeDirectWait ||
            subscription.target == SignalSubscription::DesignDirectWait;
        return !direct || signalEdgeMatches(subscription.edge, edges);
      },
      outSequence);
}

namespace {

bool latchConditionalSignalWaitersImpl(obelisk_rt_context *context,
                                       uint64_t bitOffset, uint32_t edges) {
  if (!obelisk_rt_has_conditional_signal_waiters(context))
    return true;
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
          static_cast<uint64_t>(offset) + bitIndex >= found->second.bitWidth)
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
              ? nativeHandleOffset(entry.stable_id, static_cast<int64_t>(index))
              : UINT64_MAX;
      auto snapshot = indexed == UINT64_MAX
                          ? context->signalValueSnapshots.end()
                          : context->signalValueSnapshots.find(indexed);
      if (snapshot != context->signalValueSnapshots.end()) {
        value = snapshot->second.value;
        unknown = snapshot->second.unknown;
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

  for (uint64_t token : context->nativeConditionalSignalWaiters) {
    auto indexed = context->scheduledProcessIndices.find(token);
    if (indexed == context->scheduledProcessIndices.end() ||
        indexed->second >= context->scheduledProcesses.size())
      continue;
    ScheduledProcess &process = context->scheduledProcesses[indexed->second];
    if (process.token == token && process.instance && process.started) {
      bool wasTriggered = process.signalTriggered;
      consider(currentWait(process), process.suspendKind,
               process.signalTriggered);
      if (!wasTriggered && process.signalTriggered) {
        try {
          context->nativePollCandidates.insert(token);
        } catch (const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
        if (++context->schedulerSelectionGeneration == 0)
          context->schedulerSelectionGeneration = 1;
      }
    }
  }
  for (uint64_t id : context->designConditionalSignalWaiters) {
    auto indexed = context->scheduledDesignTaskIndices.find(id);
    if (indexed == context->scheduledDesignTaskIndices.end() ||
        indexed->second >= context->scheduledDesignTasks.size())
      continue;
    ScheduledDesignTask &task = context->scheduledDesignTasks[indexed->second];
    const obelisk_rt_wait_record_v1 *wait = nullptr;
    if (task.id == id && task.started && !task.terminated &&
        task.waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
        task.waitOffset <= task.frame.size() &&
        task.waitSize <= task.frame.size() - task.waitOffset) {
      wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          task.frame.data() + task.waitOffset);
    }
    bool wasTriggered = task.signalTriggered;
    consider(wait, task.suspendKind, task.signalTriggered);
    if (!wasTriggered && task.signalTriggered) {
      try {
        context->designPollCandidates.insert(id);
      } catch (const std::bad_alloc &) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
        return false;
      }
      if (++context->schedulerSelectionGeneration == 0)
        context->schedulerSelectionGeneration = 1;
    }
  }
  return true;
}

} // namespace

bool obelisk_rt_latch_conditional_signal_waiters_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint32_t edges) {
  return context && latchConditionalSignalWaitersImpl(context, stableID, edges);
}

bool obelisk_rt_latch_conditional_signal_range_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edges) {
  if (!context)
    return false;
  if (!obelisk_rt_has_conditional_signal_waiters(context))
    return true;
  for (uint64_t bit = 0; bit != bitWidth; ++bit) {
    if (bit > static_cast<uint64_t>(INT64_MAX))
      break;
    uint64_t handle = nativeHandleOffset(stableID, static_cast<int64_t>(bit));
    if (handle != UINT64_MAX &&
        !latchConditionalSignalWaitersImpl(context, handle, edges))
      return false;
  }
  return true;
}

bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown) {
  return obelisk_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown, true);
}

bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown,
                                             bool evaluateComputedObservers) {
  if (!context)
    return false;
  uint32_t edges = transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
  if (edges == 0)
    return true;
  uint64_t sequence = 0;
  if (!obelisk_rt_publish_signal_occurrence_unlocked(context, bitOffset, 1,
                                                     edges, &sequence))
    return false;
  if (obelisk_rt_has_conditional_signal_waiters(context))
    context->signalValueSnapshots[bitOffset] = {sequence, newValue, newUnknown};
  if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(context, bitOffset,
                                                            edges))
    return false;
  if (evaluateComputedObservers &&
      !obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, 1))
    return false;
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

  uint64_t headerSize;
  uint64_t frameOffset;
  uint64_t tailSize;
  uint64_t totalSize;
  uint64_t frameAlignment = std::max<uint64_t>(
      descriptor->frame_layout->frame_alignment, nativeAlignment);
  uint64_t allocationAlignment = std::max<uint64_t>(
      {alignof(obelisk_rt_process_instance_v1), frameAlignment, uint64_t{16}});
  if (addOverflow(kProcessAllocationMetadataOffset,
                  sizeof(ProcessAllocationMetadata), headerSize) ||
      alignUp(headerSize, frameAlignment, frameOffset) ||
      addOverflow(scratchOffset, scratchSize, tailSize) ||
      addOverflow(frameOffset, tailSize, totalSize) ||
      alignUp(totalSize, allocationAlignment, totalSize) ||
      totalSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_MEMORY;

  ProcessAllocationPool *pool = processAllocationPool();
  void *allocation =
      pool ? pool->allocate(static_cast<size_t>(totalSize),
                            static_cast<size_t>(allocationAlignment))
           : std::aligned_alloc(static_cast<size_t>(allocationAlignment),
                                static_cast<size_t>(totalSize));
  if (!allocation)
    return OBELISK_RT_OUT_OF_MEMORY;
  std::memset(allocation, 0, static_cast<size_t>(totalSize));
  void *frame = static_cast<uint8_t *>(allocation) + frameOffset;
  auto *instance = ::new (allocation)
      obelisk_rt_process_instance_v1{descriptor,
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
  ::new (static_cast<uint8_t *>(allocation) + kProcessAllocationMetadataOffset)
      ProcessAllocationMetadata{
          kProcessAllocationMagic, static_cast<size_t>(totalSize),
          static_cast<size_t>(allocationAlignment), nullptr};
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
    obelisk_rt_status status =
        instance->descriptor->bytecode
            ? obelisk_rt_validate_bytecode_program(
                  *instance->descriptor->bytecode, instance->continuation)
            : obelisk_rt_validate_design_bytecode(
                  *instance->descriptor->design_bytecode, nullptr, nullptr);
    if (status != OBELISK_RT_OK)
      return status;
  }

  if (instance->tier != 0 && instance->tier != requestedTier &&
      instance->tier == OBELISK_RT_TIER_NATIVE && instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  ManagedExecutionScope managedExecution(context);
  if (managedExecution.getStatus() != OBELISK_RT_OK)
    return managedExecution.getStatus();
  bool installedActiveProcess = false;
  bool installedOwnership = false;
  if (context) {
    ContextMutexLock lock(context);
    if (context->activeNativeProcess &&
        context->activeNativeProcess != instance)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (instance->ownership_context && instance->ownership_context != context)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!context->activeNativeProcess) {
      context->activeNativeProcess = instance;
      installedActiveProcess = true;
    }
    installedOwnership = !instance->ownership_context;
    instance->ownership_context = context;
  }
  obelisk_rt_status rootStatus = guarded(
      context, [&] { return registerManagedFrameRoots(instance, context); });
  if (rootStatus != OBELISK_RT_OK) {
    if (installedActiveProcess) {
      ContextMutexLock lock(context);
      if (context->activeNativeProcess == instance)
        context->activeNativeProcess = nullptr;
    }
    if (installedOwnership)
      instance->ownership_context = nullptr;
    return rootStatus;
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
    ContextMutexLock lock(context);
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
      instance->ownership_context) {
    unregisterManagedFrameRoots(instance);
    releaseOwnedNativeStates(instance->ownership_context, instance);
  }
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
  unregisterManagedFrameRoots(instance);
  if (instance->native_handle) {
    instance->descriptor->native_destroy(instance);
    if (instance->native_handle)
      return OBELISK_RT_INVALID_LIFECYCLE;
  }
  auto *metadata = processMetadata(instance);
  if (metadata->magic != kProcessAllocationMagic)
    return OBELISK_RT_INVALID_LIFECYCLE;
  size_t allocationSize = metadata->size;
  size_t allocationAlignment = metadata->alignment;
  void *allocation = instance;
  metadata->magic = 0;
  std::destroy_at(metadata);
  std::destroy_at(instance);
  if (ProcessAllocationPool *pool = processAllocationPool())
    pool->release(allocation, allocationSize, allocationAlignment);
  else
    std::free(allocation);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_add(obelisk_rt_context *context,
                            obelisk_rt_process_instance_v1 *instance,
                            uint32_t flags) {
  return obelisk_rt_v1_scheduler_add_ranked(context, instance, flags,
                                            UINT32_MAX);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_add_ranked(obelisk_rt_context *context,
                                   obelisk_rt_process_instance_v1 *instance,
                                   uint32_t flags, uint32_t scheduleRank) {
  return obelisk_rt_v1_scheduler_add_planned(context, instance, flags,
                                             scheduleRank, nullptr, nullptr, 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_planned(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t initialRank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuationCount) {
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  if (!context || !instance ||
      !obelisk_rt_decode_schedule_flags(flags, phase, homeRegion))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (continuationCount != 0 && (!continuations || !ranks))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != continuationCount; ++index)
    if (continuations[index] == 0 ||
        (index != 0 && continuations[index - 1] >= continuations[index]))
      return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
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
    obelisk_rt_random_split_unlocked(context, process.random);
    process.controls = context->activeControls;
    process.insertionSequence = context->nextProcessInsertionSequence++;
    process.observedEpoch = context->schedulerEpoch;
    process.phase = phase;
    process.homeRegion = homeRegion;
    process.queuedRegion = homeRegion;
    process.scheduleRank = initialRank;
    if (context->execution && (context->execution->flags &
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
    uint64_t token = context->scheduledProcesses.back().token;
    try {
      context->scheduledProcessIndices[token] =
          context->scheduledProcesses.size() - 1;
      context->nativePollCandidates.insert(token);
    } catch (...) {
      context->scheduledProcessIndices.erase(token);
      context->nativePollCandidates.erase(token);
      context->scheduledProcesses.pop_back();
      throw;
    }
    obelisk_rt_retain_controls_unlocked(
        context, context->scheduledProcesses.back().controls);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_install_aot(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_plan *plan) {
  if (!context || !plan ||
      plan->size != sizeof(obelisk_rt_native_schedule_plan))
    return OBELISK_RT_INVALID_ARGUMENT;
  bool actorStorageFits = plan->mutable_state_size >=
                          uint64_t{plan->actor_capacity} * sizeof(void *);
  bool nbaTablesValid =
      plan->nba_reserved == 0 &&
      ((plan->nba_root_count == 0 && plan->nba_roots == nullptr) ||
       (plan->nba_root_count != 0 && plan->nba_roots != nullptr)) &&
      ((plan->nba_site_count == 0 && plan->nba_sites == nullptr) ||
       (plan->nba_site_count != 0 && plan->nba_sites != nullptr)) &&
      (plan->nba_site_count == 0 || plan->nba_root_count != 0) &&
      plan->nba_site_count <= std::numeric_limits<size_t>::max();
  bool fanoutTableValid =
      (plan->fanout_entry_count == 0 && plan->fanout_entries == nullptr) ||
      (plan->fanout_entry_count != 0 && plan->fanout_entries != nullptr);
  bool actorRootTableValid =
      (plan->actor_root_count == 0 && plan->actor_roots == nullptr) ||
      (plan->actor_root_count != 0 && plan->actor_roots != nullptr);
  bool nbaCommitValid =
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA) != 0) ==
      (plan->nba_commit != nullptr);
  bool specializationFastValid =
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION) !=
       0) == (plan->specialization_fast != nullptr) &&
      ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION) == 0 ||
       (plan->flags & (OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA)) != 0);
  bool statePlanesValid =
      plan->state_bit_count == 0
          ? plan->state_value == nullptr && plan->state_unknown == nullptr
          : plan->state_value != nullptr && plan->state_unknown != nullptr;
  if (!plan->mutable_state || plan->mutable_state_size == 0 ||
      plan->actor_capacity == 0 || !actorStorageFits || !statePlanesValid ||
      !nbaTablesValid || !fanoutTableValid || !actorRootTableValid ||
      !nbaCommitValid || !specializationFastValid ||
      (plan->flags & ~(OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC |
                       OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT |
                       OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE |
                       OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA |
                       OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS |
                       OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT |
                       OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION)) !=
          0 ||
      !plan->bind || !plan->run || !plan->fallback_snapshot)
    return OBELISK_RT_INVALID_ARGUMENT;
  const obelisk_rt_static_nba_root *nbaRoots = plan->nba_roots;
  uint32_t nbaRootCount = plan->nba_root_count;
  const obelisk_rt_static_nba_site *nbaSites = plan->nba_sites;
  uint64_t nbaSiteCount = plan->nba_site_count;
  const obelisk_rt_static_fanout_entry *fanoutEntries = plan->fanout_entries;
  uint64_t fanoutEntryCount = plan->fanout_entry_count;
  const obelisk_rt_static_actor_root *actorRoots = plan->actor_roots;
  uint64_t actorRootCount = plan->actor_root_count;
  for (uint32_t index = 0; index != nbaRootCount; ++index) {
    const obelisk_rt_static_nba_root &root = nbaRoots[index];
    if (root.static_state == 0 || root.bit_width == 0 ||
        root.bit_width > UINT64_MAX - 63 ||
        (root.bit_width + 7) / 8 > std::numeric_limits<size_t>::max() ||
        (root.generated_accumulator &&
         (root.bit_width <= 64 || root.bit_width > 256)))
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint32_t previous = 0; previous != index; ++previous)
      if (nbaRoots[previous].static_state == root.static_state)
        return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != nbaSiteCount; ++index) {
    const obelisk_rt_static_nba_site &site = nbaSites[index];
    if (site.site == UINT64_MAX || site.root >= nbaRootCount ||
        (site.storage != OBELISK_RT_STATIC_NBA_FIXED_SLOT &&
         site.storage != OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR) ||
        (index != 0 && nbaSites[index - 1].site >= site.site))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != fanoutEntryCount; ++index) {
    const obelisk_rt_static_fanout_entry &entry = fanoutEntries[index];
    if (entry.static_state == 0 || entry.actor_slot >= plan->actor_capacity ||
        entry.continuation == 0 || entry.bit_width == 0 ||
        entry.low_bit > UINT64_MAX - entry.bit_width ||
        entry.edge < OBELISK_RT_WAIT_EDGE_CHANGE ||
        entry.edge > OBELISK_RT_WAIT_EDGE_BOTH ||
        (index != 0 && std::tuple{fanoutEntries[index - 1].static_state,
                                  fanoutEntries[index - 1].low_bit,
                                  fanoutEntries[index - 1].actor_slot,
                                  fanoutEntries[index - 1].continuation} >=
                           std::tuple{entry.static_state, entry.low_bit,
                                      entry.actor_slot, entry.continuation}))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  for (uint64_t index = 0; index != actorRootCount; ++index) {
    const obelisk_rt_static_actor_root &entry = actorRoots[index];
    if (entry.actor_slot >= plan->actor_capacity || entry.static_state == 0 ||
        entry.reserved != 0 || entry.flags == 0 ||
        (entry.flags &
         ~(OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE)) != 0 ||
        !findNativeStaticState(context, entry.static_state) ||
        (index != 0 &&
         std::tuple{actorRoots[index - 1].actor_slot,
                    actorRoots[index - 1].static_state,
                    actorRoots[index - 1].flags} >=
             std::tuple{entry.actor_slot, entry.static_state, entry.flags}))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->nativeSchedulePlan || !context->scheduledProcesses.empty() ||
        !context->scheduledDesignTasks.empty() ||
        context->activeNativeProcess || context->designTaskExecuting)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->execution && context->execution->checksum != 0 &&
        plan->graph_layout_checksum != context->execution->checksum)
      return OBELISK_RT_LAYOUT_MISMATCH;
    if (plan->state_bit_count != 0 &&
        !validNativeStatePlanesUnlocked(context, plan->state_value,
                                        plan->state_unknown,
                                        plan->state_bit_count))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (uint32_t index = 0; index != nbaRootCount; ++index) {
      const obelisk_rt_static_nba_root &root = nbaRoots[index];
      const NativeStaticState *state =
          findNativeStaticState(context, root.static_state);
      if (!state || state->bitWidth != root.bit_width)
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
    for (uint64_t index = 0; index != fanoutEntryCount; ++index) {
      const obelisk_rt_static_fanout_entry &entry = fanoutEntries[index];
      const NativeStaticState *state =
          findNativeStaticState(context, entry.static_state);
      if (!state || entry.low_bit > state->bitWidth ||
          entry.bit_width > state->bitWidth - entry.low_bit)
        return OBELISK_RT_LAYOUT_MISMATCH;
    }
    {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      if (!installedNativeScheduleStates.insert(plan->mutable_state).second)
        return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (plan->state_bit_count != 0 &&
        !importNativeStatePlanesUnlocked(context, plan->state_value,
                                         plan->state_unknown,
                                         plan->state_bit_count)) {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      installedNativeScheduleStates.erase(plan->mutable_state);
      return OBELISK_RT_LAYOUT_MISMATCH;
    }
    try {
#if defined(__x86_64__) || defined(_M_X64)
      __builtin_cpu_init();
      context->nativeScheduleAVX2 = __builtin_cpu_supports("avx2");
#else
      context->nativeScheduleAVX2 = false;
#endif
      context->nativeScheduleActors.assign(plan->actor_capacity, nullptr);
      context->nativeScheduleActorTokens.assign(plan->actor_capacity, 0);
      context->nativeScheduleActorIndices.assign(plan->actor_capacity,
                                                 SIZE_MAX);
      context->nativeScheduleNodes.clear();
      context->nativeScheduleActorNodes.clear();
      context->nativeScheduleReadyNodes.clear();
      context->nativeScheduleDeadlines.assign(plan->actor_capacity, UINT64_MAX);
      context->nativeScheduleDeadlineHeap.clear();
      context->nativeScheduleDeadlineHeap.reserve(plan->actor_capacity);
      context->nativeScheduleDeadlinePositions.assign(plan->actor_capacity,
                                                      UINT32_MAX);
      uint32_t maximumStaticID = 0;
      for (const auto &[id, state] : context->nativeStaticStates) {
        (void)state;
        maximumStaticID = std::max(maximumStaticID, id);
      }
      if (maximumStaticID <= context->nativeStaticStates.size()) {
        context->nativeScheduleStaticStateIndex.assign(
            static_cast<size_t>(maximumStaticID) + 1, {});
        for (const auto &[id, state] : context->nativeStaticStates)
          context->nativeScheduleStaticStateIndex[id] = state;
      }
      context->nativeScheduleTransientDirtyMask.assign(
          context->nativeScheduleStaticStateIndex.size(), 0);
      context->nativeSchedulePersistentDirtyMask.assign(
          context->nativeScheduleStaticStateIndex.size(), 0);
      context->staticNBAAccumulators.clear();
      context->staticNBAAccumulators.reserve(nbaRootCount);
      context->staticNBASlowRoots.assign(nbaRootCount, 0);
      context->staticNBARootHasFanout.assign(nbaRootCount, 0);
      for (uint32_t index = 0; index != nbaRootCount; ++index) {
        const obelisk_rt_static_nba_root &root = nbaRoots[index];
        const obelisk_rt_static_fanout_entry *fanout = std::lower_bound(
            fanoutEntries, fanoutEntries + fanoutEntryCount, root.static_state,
            [](const auto &entry, uint32_t staticState) {
              return entry.static_state < staticState;
            });
        if (fanout != fanoutEntries + fanoutEntryCount &&
            fanout->static_state == root.static_state)
          context->staticNBARootHasFanout[index] = 1;
        size_t words = static_cast<size_t>((root.bit_width + 63) / 64);
        StaticNBAAccumulator accumulator;
        accumulator.value.assign(words, 0);
        accumulator.unknown.assign(words, 0);
        accumulator.writeMask.assign(words, 0);
        accumulator.changed.assign(words, 0);
        accumulator.posedge.assign(words, 0);
        accumulator.negedge.assign(words, 0);
        context->staticNBAAccumulators.push_back(std::move(accumulator));
        if (root.generated_accumulator)
          *root.generated_accumulator = {};
      }
      context->nativeScheduleNBASiteIndex.clear();
      uint64_t denseSiteLimit = std::min<uint64_t>(
          UINT64_C(1) << 20,
          std::max<uint64_t>(1024, nbaSiteCount <= UINT64_MAX / 4
                                       ? nbaSiteCount * 4
                                       : UINT64_MAX));
      if (nbaSiteCount != 0 &&
          nbaSites[nbaSiteCount - 1].site <= denseSiteLimit) {
        context->nativeScheduleNBASiteIndex.assign(
            static_cast<size_t>(nbaSites[nbaSiteCount - 1].site) + 1,
            UINT32_MAX);
        for (uint64_t index = 0; index != nbaSiteCount; ++index)
          context->nativeScheduleNBASiteIndex[nbaSites[index].site] =
              nbaSites[index].root;
      }
    } catch (...) {
      std::lock_guard<std::mutex> registryLock(nativeScheduleRegistryMutex);
      installedNativeScheduleStates.erase(plan->mutable_state);
      throw;
    }
    context->nativeSchedulePlan = plan;
    if (plan->specialization_fast)
      *plan->specialization_fast = 0;
    context->nativeScheduleNBARoots = nbaRoots;
    context->nativeScheduleNBARootCount = nbaRootCount;
    context->nativeScheduleNBASites = nbaSites;
    context->nativeScheduleNBASiteCount = nbaSiteCount;
    context->nativeScheduleFanoutEntries = fanoutEntries;
    context->nativeScheduleFanoutEntryCount = fanoutEntryCount;
    context->nativeScheduleActorRoots = actorRoots;
    context->nativeScheduleActorRootCount = actorRootCount;
    context->nativeScheduleDeoptimized = false;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_add_aot(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t actorSlot, uint32_t initialRank,
    const uint32_t *continuations, const uint32_t *ranks,
    uint32_t continuationCount, const uint32_t *bytecodeContinuations,
    uint32_t bytecodeContinuationCount) {
  if (!context || !instance)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (bytecodeContinuationCount != 0 && !bytecodeContinuations)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (bytecodeContinuationCount != 0 && (instance->descriptor->available_tiers &
                                         OBELISK_RT_TIER_MASK_BYTECODE) == 0)
    return OBELISK_RT_TIER_UNAVAILABLE;
  for (uint32_t index = 0; index != bytecodeContinuationCount; ++index)
    if (!validContinuation(*instance->descriptor->frame_layout,
                           bytecodeContinuations[index]) ||
        (index != 0 &&
         bytecodeContinuations[index - 1] >= bytecodeContinuations[index]))
      return OBELISK_RT_INVALID_CONTINUATION;
  ContextTransaction transaction(context);
  const obelisk_rt_native_schedule_plan *plan = nullptr;
  {
    ContextMutexLock lock(context);
    plan = context->nativeSchedulePlan;
    if (!plan || context->nativeScheduleDeoptimized ||
        actorSlot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleActors[actorSlot])
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_status status = obelisk_rt_v1_scheduler_add_planned(
      context, instance, flags, initialRank, continuations, ranks,
      continuationCount);
  if (status != OBELISK_RT_OK)
    return status;
  auto rollback = [&] {
    ContextMutexLock lock(context);
    auto found = std::find_if(context->scheduledProcesses.begin(),
                              context->scheduledProcesses.end(),
                              [&](const ScheduledProcess &process) {
                                return process.instance == instance;
                              });
    if (found == context->scheduledProcesses.end())
      return;
    obelisk_rt_release_controls_unlocked(context, found->controls);
    context->nativePollCandidates.erase(found->token);
    context->scheduledProcessIndices.erase(found->token);
    context->scheduledProcesses.erase(found);
    rebuildNativeSchedulerIndexUnlocked(context);
  };
  uint64_t actorToken = 0;
  try {
    ContextMutexLock lock(context);
    auto found = std::find_if(context->scheduledProcesses.begin(),
                              context->scheduledProcesses.end(),
                              [&](const ScheduledProcess &process) {
                                return process.instance == instance;
                              });
    if (found == context->scheduledProcesses.end())
      status = OBELISK_RT_INVALID_LIFECYCLE;
    else {
      if (bytecodeContinuationCount == 0)
        found->bytecodeContinuations.clear();
      else
        found->bytecodeContinuations.assign(bytecodeContinuations,
                                            bytecodeContinuations +
                                                bytecodeContinuationCount);
      found->aotActorSlot = actorSlot;
      actorToken = found->token;
    }
  } catch (const std::bad_alloc &) {
    status = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    status = OBELISK_RT_INVALID_ARGUMENT;
  }
  if (status != OBELISK_RT_OK) {
    rollback();
    return status;
  }
  status = plan->bind(plan->mutable_state, context, actorSlot, instance);
  bool bound = status == OBELISK_RT_OK;
  if (bound) {
    ContextMutexLock lock(context);
    ScheduledProcess *scheduled = findScheduledProcess(context, actorToken);
    if (context->nativeSchedulePlan != plan ||
        context->nativeScheduleActors[actorSlot] || !scheduled ||
        scheduled->instance != instance) {
      status = OBELISK_RT_INVALID_LIFECYCLE;
    } else {
      auto index = context->scheduledProcessIndices.find(actorToken);
      if (index == context->scheduledProcessIndices.end()) {
        status = OBELISK_RT_INVALID_LIFECYCLE;
      } else {
        context->nativeScheduleActors[actorSlot] = instance;
        context->nativeScheduleActorTokens[actorSlot] = actorToken;
        context->nativeScheduleActorIndices[actorSlot] = index->second;
        bool nativeRootBootstrap =
            actorSlot == 0 &&
            (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO) != 0;
        if (!nativeRootBootstrap &&
            context->nativeScheduleExternalWritePending &&
            nativeAOTActorDirty(context, actorSlot) &&
            (instance->descriptor->available_tiers &
             OBELISK_RT_TIER_MASK_BYTECODE) != 0)
          instance->tier = OBELISK_RT_TIER_BYTECODE;
        if (!markNativeAOTActorReadyUnlocked(context, actorSlot))
          status = OBELISK_RT_INVALID_CONTINUATION;
        else
          return OBELISK_RT_OK;
      }
    }
  }
  if (bound) {
    obelisk_rt_status clearStatus =
        plan->bind(plan->mutable_state, context, actorSlot, nullptr);
    if (clearStatus != OBELISK_RT_OK)
      status = clearStatus;
    ContextMutexLock lock(context);
    context->nativeScheduleActors[actorSlot] = nullptr;
    context->nativeScheduleActorTokens[actorSlot] = 0;
    context->nativeScheduleActorIndices[actorSlot] = SIZE_MAX;
    removeNativeAOTDeadlineUnlocked(context, actorSlot);
  }
  // Roll back the generic ownership record as one transaction. The caller
  // retains ownership when add_aot fails.
  rollback();
  return status;
}

extern "C" uint64_t obelisk_rt_v1_scheduler_process_token(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance) {
  if (!context || !instance)
    return 0;
  try {
    ContextMutexLock lock(context);
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance == instance)
        return process.token;
  } catch (...) {
  }
  return 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_monitor_register(obelisk_rt_context *context,
                               uint64_t processToken, uint32_t designProcess) {
  if (!context || processToken == 0 || designProcess > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextMutexLock lock(context);
    uint64_t replacement =
        designProcess ? processToken
                      : OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | processToken;
    uint64_t previous = context->monitorLogicalProcessToken;
    context->monitorLogicalProcessToken = replacement;
    context->monitorEnabled = true;
    if (previous != replacement)
      wakeMonitorProcessUnlocked(context, previous);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_monitor_control(obelisk_rt_context *context, uint32_t enabled) {
  if (!context || enabled > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextMutexLock lock(context);
    context->monitorEnabled = enabled != 0;
    if (enabled)
      wakeMonitorProcessUnlocked(context, context->monitorLogicalProcessToken);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t obelisk_rt_v1_monitor_current(obelisk_rt_context *context) {
  if (!context)
    return 0;
  try {
    ContextMutexLock lock(context);
    return context->activeLogicalProcessToken != 0 &&
                   context->activeLogicalProcessToken ==
                       context->monitorLogicalProcessToken
               ? 1
               : 0;
  } catch (...) {
    return 0;
  }
}

std::optional<uint64_t> countGeneratedNBAStages(
    const obelisk_rt_generated_nba_accumulator_256 &generated) {
  uint64_t writtenLanes = 0;
  for (uint64_t mask : generated.write_mask)
    for (unsigned shift : {0u, 32u}) {
      uint32_t lane = static_cast<uint32_t>(mask >> shift);
      if (lane == 0)
        continue;
      if (lane != UINT32_MAX)
        return std::nullopt;
      ++writtenLanes;
    }
  if (writtenLanes == 0)
    return std::nullopt;
  return writtenLanes;
}

bool hasGeneratedNBAStages(
    const obelisk_rt_generated_nba_accumulator_256 &generated) {
  return generated.valid != 0;
}

static obelisk_rt_status materializeGeneratedNBAAccumulatorUnlocked(
    obelisk_rt_context *context, uint32_t rootIndex, uint32_t execRegion);

static obelisk_rt_status
schedulerNBA(obelisk_rt_context *context, uint8_t *valuePlane,
             uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t bitOffset,
             uint64_t bitWidth, uint64_t delay, const uint8_t *value,
             const uint8_t *unknown, bool stringValue,
             uint64_t staticSite = UINT64_MAX) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto fail = [&](obelisk_rt_status status) {
    obelisk_rt_v1_scheduler_fail(context, status);
    return status;
  };
  if (!valuePlane || bitWidth == 0 || (bitWidth + 7) < bitWidth ||
      (stringValue && (bitWidth != 64 || unknownPlane)))
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (bitOffset == UINT64_MAX)
    return OBELISK_RT_OK;
  ContextTransaction transaction(context);
  uint32_t automaticID = 0;
  uint32_t staticID = 0;
  int64_t offset = 0;
  bool automatic = decodeNativeAutomatic(bitOffset, automaticID, offset);
  bool boundedStatic =
      !automatic && decodeNativeStatic(bitOffset, staticID, offset);
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
  obelisk_rt_string_v1 queuedString = 0;
  if (stringValue) {
    std::memcpy(&queuedString, value, sizeof(queuedString));
    obelisk_rt_status status =
        obelisk_rt_validate_string(context, queuedString);
    if (status != OBELISK_RT_OK)
      return fail(status);
  }
  try {
    ScheduledNBA update;
    update.valuePlane = valuePlane;
    update.unknownPlane = unknownPlane;
    update.planeBitCount = planeBitCount;
    update.bitOffset = bitOffset;
    update.bitWidth = bitWidth;
    update.stringValue = stringValue;
    update.rootedString = queuedString;
    ContextMutexLock lock(context);
    const NativeStaticState *staticState = nullptr;
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
      staticState = findNativeStaticState(context, staticID);
      if (!staticState || staticState->bitOffset > planeBitCount ||
          staticState->bitWidth > planeBitCount - staticState->bitOffset ||
          offset >= static_cast<__int128>(staticState->bitWidth) ||
          static_cast<__int128>(offset) + bitWidth <= 0) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
    }
    update.execRegion = obelisk_rt_commit_region(
        context->activeHomeRegion == UINT32_MAX
            ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
            : context->activeHomeRegion);
    if (update.execRegion == UINT32_MAX) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (staticSite != UINT64_MAX && boundedStatic && !stringValue &&
        delay == 0 && context->nativeSchedulePlan &&
        !context->nativeScheduleDeoptimized &&
        context->nativeScheduleNBASiteCount != 0) {
      uint32_t staticRoot = UINT32_MAX;
      if (staticSite < context->nativeScheduleNBASiteIndex.size()) {
        staticRoot = context->nativeScheduleNBASiteIndex[staticSite];
      } else {
        const obelisk_rt_static_nba_site *begin =
            context->nativeScheduleNBASites;
        const obelisk_rt_static_nba_site *end =
            begin + context->nativeScheduleNBASiteCount;
        const obelisk_rt_static_nba_site *found = std::lower_bound(
            begin, end, staticSite,
            [](const auto &entry, uint64_t id) { return entry.site < id; });
        if (found != end && found->site == staticSite)
          staticRoot = found->root;
      }
      if (staticRoot == UINT32_MAX ||
          staticRoot >= context->staticNBAAccumulators.size() ||
          staticRoot >= context->nativeScheduleNBARootCount)
        return fail(OBELISK_RT_INVALID_DESIGN);
      const obelisk_rt_static_nba_root &root =
          context->nativeScheduleNBARoots[staticRoot];
      if (root.static_state != staticID ||
          root.bit_width != staticState->bitWidth)
        return fail(OBELISK_RT_LAYOUT_MISMATCH);
      if (root.generated_accumulator) {
        // A source-ordered generic site may follow generated direct stages for
        // the same root. Materialize first so the generic write remains the
        // last write at the barrier.
        if (obelisk_rt_status status =
                materializeGeneratedNBAAccumulatorUnlocked(context, staticRoot,
                                                           update.execRegion);
            status != OBELISK_RT_OK)
          return fail(status);
      }
      if (staticRoot >= context->staticNBASlowRoots.size())
        return fail(OBELISK_RT_INVALID_DESIGN);
      bool rootDirty =
          context->nativeScheduleTransientDirtyRoots.find(root.static_state) !=
              context->nativeScheduleTransientDirtyRoots.end() ||
          context->nativeSchedulePersistentDirtyRoots.find(root.static_state) !=
              context->nativeSchedulePersistentDirtyRoots.end();
      if (!rootDirty && context->staticNBASlowRoots[staticRoot] == 0) {
        // Both v1 storage classes are immediate and root-bounded. FixedSlot
        // proves site uniqueness to the compiler; after value capture it can
        // share the root accumulator's ordered last-write merge.
        StaticNBAAccumulator &accumulator =
            context->staticNBAAccumulators[staticRoot];
        if (accumulator.valid &&
            (accumulator.valuePlane != valuePlane ||
             accumulator.unknownPlane != unknownPlane ||
             accumulator.planeBitCount != planeBitCount ||
             accumulator.execRegion != update.execRegion)) {
          context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
          return OBELISK_RT_INVALID_LIFECYCLE;
        }
        accumulator.valuePlane = valuePlane;
        accumulator.unknownPlane = unknownPlane;
        accumulator.planeBitCount = planeBitCount;
        accumulator.execRegion = update.execRegion;
        accumulator.valid = true;
        bool packedStage =
            bitWidth <= 64 && offset >= 0 &&
            static_cast<uint64_t>(offset) <= root.bit_width &&
            bitWidth <= root.bit_width - static_cast<uint64_t>(offset);
        if (packedStage) {
          uint64_t packedValue = 0;
          uint64_t packedUnknown = 0;
          for (uint64_t byte = 0; byte != byteCount; ++byte) {
            packedValue |= uint64_t{value[byte]} << (byte * 8);
            if (unknownPlane)
              packedUnknown |= uint64_t{unknown[byte]} << (byte * 8);
          }
          uint64_t sourceMask = packedWidthMask(bitWidth);
          packedValue &= sourceMask;
          packedUnknown &= sourceMask;
          uint64_t destination = static_cast<uint64_t>(offset);
          size_t word = static_cast<size_t>(destination / 64);
          unsigned shift = static_cast<unsigned>(destination % 64);
          uint64_t lowMask = sourceMask << shift;
          accumulator.value[word] =
              (accumulator.value[word] & ~lowMask) | (packedValue << shift);
          accumulator.unknown[word] =
              (accumulator.unknown[word] & ~lowMask) | (packedUnknown << shift);
          accumulator.writeMask[word] |= lowMask;
          if (shift != 0 && bitWidth > 64 - shift) {
            uint64_t highMask = sourceMask >> (64 - shift);
            accumulator.value[word + 1] =
                (accumulator.value[word + 1] & ~highMask) |
                (packedValue >> (64 - shift));
            accumulator.unknown[word + 1] =
                (accumulator.unknown[word + 1] & ~highMask) |
                (packedUnknown >> (64 - shift));
            accumulator.writeMask[word + 1] |= highMask;
          }
        } else {
          __int128 firstWide =
              std::max<__int128>(0, -static_cast<__int128>(offset));
          __int128 lastWide = std::min<__int128>(
              bitWidth, static_cast<__int128>(root.bit_width) - offset);
          if (firstWide >= lastWide) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return OBELISK_RT_INVALID_HANDLE;
          }
          uint64_t first = static_cast<uint64_t>(firstWide);
          uint64_t last = static_cast<uint64_t>(lastWide);
          auto *accValue =
              reinterpret_cast<uint8_t *>(accumulator.value.data());
          auto *accUnknown =
              reinterpret_cast<uint8_t *>(accumulator.unknown.data());
          auto *accMask =
              reinterpret_cast<uint8_t *>(accumulator.writeMask.data());
          for (uint64_t source = first; source < last; ++source) {
            uint64_t destination =
                static_cast<uint64_t>(static_cast<__int128>(offset) + source);
            setByteBit(accValue, destination, byteBit(value, source));
            setByteBit(accUnknown, destination,
                       unknownPlane && byteBit(unknown, source));
            setByteBit(accMask, destination, true);
          }
        }
        accumulator.sequence = context->nextSchedulerSequence++;
        ++context->signalDiagnostics.aotNBAStages;
        return OBELISK_RT_OK;
      }
    }
    if (boundedStatic && !stringValue && delay == 0 &&
        context->nativeSchedulePlan) {
      for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
           ++root)
        if (context->nativeScheduleNBARoots[root].static_state == staticID) {
          if (root >= context->staticNBASlowRoots.size())
            return fail(OBELISK_RT_INVALID_DESIGN);
          if (obelisk_rt_status status =
                  materializeGeneratedNBAAccumulatorUnlocked(context, root,
                                                             update.execRegion);
              status != OBELISK_RT_OK)
            return fail(status);
          context->staticNBASlowRoots[root] = 1;
          invalidateNativeStaticSpecializationFastUnlocked(context);
          break;
        }
    }
    update.inlinePacked =
        !automatic && boundedStatic && !stringValue && delay == 0 &&
        bitWidth <= 64 && offset >= 0 && staticState &&
        static_cast<uint64_t>(offset) <= staticState->bitWidth &&
        bitWidth <= staticState->bitWidth - static_cast<uint64_t>(offset) &&
        context->nativeSchedulePlan && !context->nativeScheduleDeoptimized &&
        (context->nativeSchedulePlan->flags &
         OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0;
    if (update.inlinePacked) {
      for (uint64_t byte = 0; byte != byteCount; ++byte)
        update.inlineValue |= uint64_t{value[byte]} << (byte * 8);
      if (unknownPlane)
        for (uint64_t byte = 0; byte != byteCount; ++byte)
          update.inlineUnknown |= uint64_t{unknown[byte]} << (byte * 8);
      ++context->signalDiagnostics.aotNBAStages;
    } else {
      update.value.assign(value, value + static_cast<size_t>(byteCount));
      if (unknownPlane)
        update.unknown.assign(unknown,
                              unknown + static_cast<size_t>(byteCount));
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

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint8_t *unknownPlane,
    uint64_t planeBitCount, uint64_t bitOffset, uint64_t bitWidth,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown) {
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, delay, value, unknown, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_static_nba(
    obelisk_rt_context *context, uint64_t site, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t bitOffset,
    uint64_t bitWidth, const uint8_t *value, const uint8_t *unknown) {
  if (site == UINT64_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  return schedulerNBA(context, valuePlane, unknownPlane, planeBitCount,
                      bitOffset, bitWidth, 0, value, unknown, false, site);
}

static obelisk_rt_status stageStaticNBAPacked(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown, bool guardedClaim) {
  if (!context || activeNativeAOTContext != context || !valuePlane ||
      bitWidth == 0 || bitWidth > 64 ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (guardedClaim ? !context->nativeSchedulePlan ||
                         (context->nativeSchedulePlan->flags &
                          OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA) == 0 ||
                         context->nativeScheduleDeoptimized
                   : !isStaticControlAOT(context) ||
                         context->nativeScheduleDeoptimized ||
                         context->nativeScheduleExternalWritePending)
    return OBELISK_RT_TIER_UNAVAILABLE;
  if (context->schedulerStatus != OBELISK_RT_OK)
    return context->schedulerStatus;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!staticState || staticState->bitWidth != root.bit_width ||
      staticState->bitOffset > planeBitCount ||
      root.bit_width > planeBitCount - staticState->bitOffset || !plan ||
      valuePlane != plan->state_value ||
      (unknownPlane && unknownPlane != plan->state_unknown) ||
      planeBitCount != plan->state_bit_count || rootOffset > root.bit_width ||
      bitWidth > root.bit_width - rootOffset) {
    context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
    return context->schedulerStatus;
  }
  if (guardedClaim) {
    if (rootIndex >= context->staticNBASlowRoots.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return context->schedulerStatus;
    }
    if (nativeStaticRootDirty(context, root.static_state))
      context->staticNBASlowRoots[rootIndex] = 1;
    if (context->staticNBASlowRoots[rootIndex] != 0) {
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return context->schedulerStatus;
      }
      if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
              context, rootIndex, execRegion);
          status != OBELISK_RT_OK) {
        context->schedulerStatus = status;
        return status;
      }
      invalidateNativeStaticSpecializationFastUnlocked(context);
      uint64_t rootHandle = obelisk_rt_stable_handle_encode(
          OBELISK_RT_STABLE_HANDLE_STATIC, root.static_state, 0);
      uint64_t destination =
          nativeHandleOffset(rootHandle, static_cast<int64_t>(rootOffset));
      return schedulerNBA(
          context, valuePlane, unknownPlane, planeBitCount, destination,
          bitWidth, 0, reinterpret_cast<const uint8_t *>(&value),
          unknownPlane ? reinterpret_cast<const uint8_t *>(&unknown) : nullptr,
          false);
    }
  }
  uint32_t execRegion = obelisk_rt_commit_region(
      context->activeHomeRegion == UINT32_MAX
          ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
          : context->activeHomeRegion);
  if (execRegion == UINT32_MAX || context->nextSchedulerSequence == 0) {
    context->schedulerStatus = execRegion == UINT32_MAX
                                   ? OBELISK_RT_INVALID_LIFECYCLE
                                   : OBELISK_RT_OUT_OF_RESOURCES;
    return context->schedulerStatus;
  }
  if (guardedClaim) {
    obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
        context, rootIndex, execRegion);
    if (status != OBELISK_RT_OK) {
      context->schedulerStatus = status;
      return status;
    }
  }

  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  if (accumulator.valid && (accumulator.valuePlane != valuePlane ||
                            accumulator.unknownPlane != unknownPlane ||
                            accumulator.planeBitCount != planeBitCount ||
                            accumulator.execRegion != execRegion)) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return context->schedulerStatus;
  }
  accumulator.valuePlane = valuePlane;
  accumulator.unknownPlane = unknownPlane;
  accumulator.planeBitCount = planeBitCount;
  accumulator.execRegion = execRegion;
  accumulator.valid = true;

  uint64_t sourceMask = packedWidthMask(bitWidth);
  value &= sourceMask;
  unknown = unknownPlane ? unknown & sourceMask : 0;
  size_t word = static_cast<size_t>(rootOffset / 64);
  unsigned shift = static_cast<unsigned>(rootOffset % 64);
  uint64_t lowMask = sourceMask << shift;
  accumulator.value[word] =
      (accumulator.value[word] & ~lowMask) | (value << shift);
  accumulator.unknown[word] =
      (accumulator.unknown[word] & ~lowMask) | (unknown << shift);
  accumulator.writeMask[word] |= lowMask;
  if (shift != 0 && bitWidth > 64 - shift) {
    uint64_t highMask = sourceMask >> (64 - shift);
    accumulator.value[word + 1] =
        (accumulator.value[word + 1] & ~highMask) | (value >> (64 - shift));
    accumulator.unknown[word + 1] =
        (accumulator.unknown[word + 1] & ~highMask) | (unknown >> (64 - shift));
    accumulator.writeMask[word + 1] |= highMask;
  }
  accumulator.sequence = context->nextSchedulerSequence++;
  ++context->signalDiagnostics.aotNBAStages;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_static_nba_packed(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown) {
  return stageStaticNBAPacked(context, rootIndex, valuePlane, unknownPlane,
                              planeBitCount, rootOffset, bitWidth, value,
                              unknown, false);
}

static obelisk_rt_status materializeGeneratedNBAAccumulatorUnlocked(
    obelisk_rt_context *context, uint32_t rootIndex, uint32_t execRegion) {
  if (rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size())
    return OBELISK_RT_INVALID_DESIGN;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if (!generated || !hasGeneratedNBAStages(*generated))
    return OBELISK_RT_OK;
  std::optional<uint64_t> stageCount = countGeneratedNBAStages(*generated);
  if (!stageCount || generated->exec_region != execRegion ||
      root.bit_width <= 64 || root.bit_width > 256)
    return OBELISK_RT_INVALID_DESIGN;
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!plan)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (!accumulator.valid) {
    if (context->nextSchedulerSequence == 0)
      return OBELISK_RT_OUT_OF_RESOURCES;
    accumulator.valuePlane = plan->state_value;
    accumulator.unknownPlane = nullptr;
    accumulator.planeBitCount = plan->state_bit_count;
    accumulator.execRegion = execRegion;
    accumulator.sequence = context->nextSchedulerSequence++;
    accumulator.valid = true;
  } else if (accumulator.execRegion != execRegion ||
             accumulator.valuePlane != plan->state_value ||
             accumulator.planeBitCount != plan->state_bit_count) {
    return OBELISK_RT_INVALID_LIFECYCLE;
  }
  size_t words = static_cast<size_t>((root.bit_width + 63) / 64);
  for (size_t word = 0; word != words; ++word) {
    uint64_t mask = generated->write_mask[word];
    accumulator.value[word] =
        (accumulator.value[word] & ~mask) | (generated->value[word] & mask);
    accumulator.unknown[word] =
        (accumulator.unknown[word] & ~mask) | (generated->unknown[word] & mask);
    accumulator.writeMask[word] |= mask;
    generated->write_mask[word] = 0;
  }
  generated->valid = 0;
  context->signalDiagnostics.aotNBAStages += *stageCount;
  return OBELISK_RT_OK;
}

extern "C" void obelisk_rt_v1_static_nba_stage_wide(
    obelisk_rt_context *context, uint32_t rootIndex, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown, uint32_t hasUnknown) {
  if (!context)
    return;
  if (activeNativeAOTContext != context || hasUnknown > 1 || bitWidth == 0 ||
      bitWidth > 64 || rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      !context->nativeSchedulePlan || context->nativeScheduleDeoptimized) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  if (rootOffset > root.bit_width || bitWidth > root.bit_width - rootOffset) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_LAYOUT_MISMATCH);
    return;
  }
  uint32_t execRegion = obelisk_rt_commit_region(
      context->activeHomeRegion == UINT32_MAX
          ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
          : context->activeHomeRegion);
  if (execRegion == UINT32_MAX) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }
  if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
          context, rootIndex, execRegion);
      status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return;
  }
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  if (!accumulator.valid) {
    if (context->nextSchedulerSequence == 0) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return;
    }
    accumulator.valuePlane = plan->state_value;
    accumulator.unknownPlane = hasUnknown ? plan->state_unknown : nullptr;
    accumulator.planeBitCount = plan->state_bit_count;
    accumulator.execRegion = execRegion;
    accumulator.sequence = context->nextSchedulerSequence++;
    accumulator.valid = true;
  }
  uint64_t sourceMask = packedWidthMask(bitWidth);
  value &= sourceMask;
  unknown = hasUnknown ? unknown & sourceMask : 0;
  size_t word = static_cast<size_t>(rootOffset / 64);
  unsigned shift = static_cast<unsigned>(rootOffset % 64);
  uint64_t lowMask = sourceMask << shift;
  accumulator.value[word] =
      (accumulator.value[word] & ~lowMask) | (value << shift);
  accumulator.unknown[word] =
      (accumulator.unknown[word] & ~lowMask) | (unknown << shift);
  accumulator.writeMask[word] |= lowMask;
  if (shift != 0 && bitWidth > 64 - shift) {
    uint64_t highMask = sourceMask >> (64 - shift);
    accumulator.value[word + 1] =
        (accumulator.value[word + 1] & ~highMask) | (value >> (64 - shift));
    accumulator.unknown[word + 1] =
        (accumulator.unknown[word + 1] & ~highMask) | (unknown >> (64 - shift));
    accumulator.writeMask[word + 1] |= highMask;
  }
  ++context->signalDiagnostics.aotNBAStages;
}

extern "C" obelisk_rt_status obelisk_rt_v1_static_nba_claim(
    obelisk_rt_context *context, uint32_t rootIndex, uint8_t *valuePlane,
    uint8_t *unknownPlane, uint64_t planeBitCount, uint64_t rootOffset,
    uint64_t bitWidth, uint64_t value, uint64_t unknown) {
  return stageStaticNBAPacked(context, rootIndex, valuePlane, unknownPlane,
                              planeBitCount, rootOffset, bitWidth, value,
                              unknown, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_string_nba(
    obelisk_rt_context *context, uint8_t *valuePlane, uint64_t planeBitCount,
    uint64_t bitOffset, uint64_t delay, obelisk_rt_string_v1 value) {
  return schedulerNBA(context, valuePlane, nullptr, planeBitCount, bitOffset,
                      64, delay, reinterpret_cast<const uint8_t *>(&value),
                      nullptr, true);
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
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
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
    if (objectWidth != 0 && (offset >= static_cast<__int128>(objectWidth) ||
                             static_cast<__int128>(offset) + bitWidth <= 0))
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(context, bitOffset,
                                                       bitWidth, edges))
      return;
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(context, bitOffset,
                                                            bitWidth, edges))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

bool publishNativeSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown) {
  uint64_t sequence = 0;
  if (!obelisk_rt_publish_signal_transition_batch_unlocked(
          context, bitOffset, bitWidth, changed, posedge, negedge, 0,
          &sequence))
    return false;
  obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset, bitWidth);
  if (obelisk_rt_has_conditional_signal_waiters(context)) {
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      context->signalValueSnapshots[eventHandle] = {
          sequence, byteBit(newValue, bit),
          newUnknown && byteBit(newUnknown, bit)};
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
      if (byteBit(posedge, bit))
        edges |= OBELISK_RT_SIGNAL_POSEDGE;
      if (byteBit(negedge, bit))
        edges |= OBELISK_RT_SIGNAL_NEGEDGE;
      if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
              context, eventHandle, edges))
        return false;
    }
  }
  if (!obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, bitWidth))
    return false;
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
  return true;
}

extern "C" void obelisk_rt_v1_scheduler_signal_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || !oldValue || !newValue)
    return;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    bool packedRange = bitWidth <= 64;
    uint32_t staticID = 0;
    int64_t staticOffset = 0;
    if (packedRange && decodeNativeStatic(bitOffset, staticID, staticOffset)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      packedRange =
          state && staticOffset >= 0 &&
          static_cast<uint64_t>(staticOffset) <= state->bitWidth &&
          bitWidth <= state->bitWidth - static_cast<uint64_t>(staticOffset);
    } else {
      packedRange = false;
    }
    if (packedRange) {
      uint64_t widthMask = packedWidthMask(bitWidth);
      uint64_t oldValueBits =
          loadPackedBytes(oldValue, 0, bitWidth) & widthMask;
      uint64_t oldUnknownBits =
          oldUnknown ? loadPackedBytes(oldUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t newValueBits =
          loadPackedBytes(newValue, 0, bitWidth) & widthMask;
      uint64_t newUnknownBits =
          newUnknown ? loadPackedBytes(newUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t changedBits =
          (oldValueBits ^ newValueBits) | (oldUnknownBits ^ newUnknownBits);
      if (changedBits == 0)
        return;
      uint64_t oldZero = ~oldUnknownBits & ~oldValueBits & widthMask;
      uint64_t oldOne = ~oldUnknownBits & oldValueBits & widthMask;
      uint64_t newZero = ~newUnknownBits & ~newValueBits & widthMask;
      uint64_t newOne = ~newUnknownBits & newValueBits & widthMask;
      uint64_t posedgeBits =
          ((oldZero & ~newZero) | (oldUnknownBits & newOne)) & widthMask;
      uint64_t negedgeBits =
          ((oldOne & ~newOne) | (oldUnknownBits & newZero)) & widthMask;
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth,
          reinterpret_cast<const uint8_t *>(&changedBits),
          reinterpret_cast<const uint8_t *>(&posedgeBits),
          reinterpret_cast<const uint8_t *>(&negedgeBits), newValue,
          newUnknown);
      return;
    }
    PackedSignalTransitionBuffer transitions(bitWidth);
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
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
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
      transitions.record(bit, edges);
      changed = true;
    }
    if (changed)
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth, transitions.changed(),
          transitions.posedge(), transitions.negedge(), newValue, newUnknown);
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_real_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint32_t bitWidth,
    const void *oldValue, const void *newValue) {
  if (!context || bitOffset == UINT64_MAX ||
      (bitWidth != 32 && bitWidth != 64) || !oldValue || !newValue)
    return;
  bool changed = false;
  if (bitWidth == 32) {
    float oldReal = 0.0f;
    float newReal = 0.0f;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  } else {
    double oldReal = 0.0;
    double newReal = 0.0;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  }
  if (!changed)
    return;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset,
                                                    bitWidth);
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    if (!obelisk_rt_notify_observer_signal_unlocked(context, bitOffset,
                                                    bitWidth))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                              uint64_t stableID,
                                              uint32_t nonblocking) {
  obelisk_rt_v1_scheduler_event_after(context, stableID, nonblocking, 0);
}

extern "C" void obelisk_rt_v1_scheduler_event_after(obelisk_rt_context *context,
                                                    uint64_t stableID,
                                                    uint32_t nonblocking,
                                                    uint64_t delay) {
  if (!context || nonblocking > 1 || (!nonblocking && delay != 0)) {
    if (context)
      obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    uint32_t retainedAutomaticID = 0;
    int64_t automaticOffset = 0;
    if (decodeNativeAutomatic(stableID, retainedAutomaticID, automaticOffset)) {
      auto found = context->nativeAutomaticStates.find(retainedAutomaticID);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return;
      }
      if (nonblocking && found->second.referenceCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
    }
    if (nonblocking) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return;
      }
      context->scheduledDesignEvents.push_back({context->nextSchedulerSequence,
                                                dueTime, execRegion, stableID,
                                                retainedAutomaticID});
      if (retainedAutomaticID != 0)
        ++context->nativeAutomaticStates.find(retainedAutomaticID)
              ->second.referenceCount;
      ++context->nextSchedulerSequence;
      return;
    }
    EventState &event = context->events[stableID];
    if (++event.generation == 0)
      event.generation = 1;
    event.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  } catch (const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_event_triggered(obelisk_rt_context *context,
                                        uint64_t stableID) {
  if (!context)
    return 0;
  try {
    ContextMutexLock lock(context);
    auto found = context->events.find(stableID);
    return found != context->events.end() && found->second.generation != 0 &&
           found->second.lastTriggeredTime == context->schedulerTime;
  } catch (...) {
    return 0;
  }
}

extern "C" void obelisk_rt_v1_scheduler_fail(obelisk_rt_context *context,
                                             obelisk_rt_status status) {
  if (!context || status == OBELISK_RT_OK)
    return;
  try {
    ContextMutexLock lock(context);
    if (context->schedulerStatus == OBELISK_RT_OK)
      context->schedulerStatus = status;
  } catch (...) {
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_register_static(obelisk_rt_context *context,
                                           uint32_t id, uint64_t bitOffset,
                                           uint64_t bitWidth) {
  if (!context || id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID ||
      bitWidth == 0 || bitOffset > UINT64_MAX - bitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
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

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_sync(obelisk_rt_context *context,
                                const uint8_t *value, const uint8_t *unknown,
                                uint64_t bitCount) {
  if (!context || !value || !unknown)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    if (!context->execution || context->execution->state_bit_count > bitCount ||
        (context->execution->state_bit_count + 7) / 8 != (bitCount + 7) / 8 ||
        !validNativeStatePlanesUnlocked(context, value, unknown,
                                        context->execution->state_bit_count))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (const auto &[id, state] : context->nativeStaticStates)
      if (state.bitOffset > context->execution->state_bit_count ||
          state.bitWidth >
              context->execution->state_bit_count - state.bitOffset)
        return OBELISK_RT_LAYOUT_MISMATCH;
    return importNativeStatePlanesUnlocked(context, value, unknown,
                                           context->execution->state_bit_count)
               ? OBELISK_RT_OK
               : OBELISK_RT_LAYOUT_MISMATCH;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

namespace {

bool staticOverrideRange(obelisk_rt_context *context, uint64_t handle,
                         uint64_t width, uint64_t &absolute) {
  uint32_t id = 0;
  int64_t offset = 0;
  if (!decodeNativeStatic(handle, id, offset) || offset < 0)
    return false;
  auto found = context->nativeStaticStates.find(id);
  if (found == context->nativeStaticStates.end() ||
      static_cast<uint64_t>(offset) > found->second.bitWidth ||
      width > found->second.bitWidth - static_cast<uint64_t>(offset))
    return false;
  absolute = found->second.bitOffset + static_cast<uint64_t>(offset);
  return context->execution &&
         absolute <= context->execution->state_bit_count &&
         width <= context->execution->state_bit_count - absolute &&
         context->stateValue.size() ==
             (context->execution->state_bit_count + 63) / 64 &&
         context->stateUnknown.size() == context->stateValue.size();
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_native_override(obelisk_rt_context *context, uint8_t *globalValue,
                              uint8_t *globalUnknown, uint64_t globalBitCount,
                              uint64_t handle, uint64_t bitWidth,
                              uint32_t descriptorKind, uint32_t assign,
                              const uint8_t *value, const uint8_t *unknown) {
  if (!context || !globalValue || !globalUnknown || !value || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || assign > 1 ||
      (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
       descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
      (assign && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
      !context->execution ||
      context->execution->state_bit_count != globalBitCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    uint64_t absolute = 0;
    uint64_t byteCount = (bitWidth + 7) / 8;
    if (byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;
    if (descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
      {
        ContextMutexLock lock(context);
        if (!staticOverrideRange(context, handle, bitWidth, absolute))
          return OBELISK_RT_INVALID_HANDLE;
      }
      return obelisk_rt_force_design_nets(context, absolute, bitWidth, value,
                                          unknown);
    }
    std::vector<uint8_t> oldValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> oldUnknown(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedUnknown(static_cast<size_t>(byteCount), 0);
    {
      ContextMutexLock lock(context);
      if (!staticOverrideRange(context, handle, bitWidth, absolute))
        return OBELISK_RT_INVALID_HANDLE;
      size_t limbs = context->stateValue.size();
      if (assign) {
        if (context->assignMask.empty()) {
          context->assignMask.assign(limbs, 0);
          context->assignValue.assign(limbs, 0);
          context->assignUnknown.assign(limbs, 0);
        }
      } else if (context->forceMask.empty()) {
        context->forceMask.assign(limbs, 0);
      }
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t destination = absolute + bit;
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t limb = destination / 64;
        bool oldV = (context->stateValue[limb] & mask) != 0;
        bool oldU = (context->stateUnknown[limb] & mask) != 0;
        bool nextV = byteBit(value, bit);
        bool nextU = unknown && byteBit(unknown, bit);
        setByteBit(oldValue.data(), bit, oldV);
        setByteBit(oldUnknown.data(), bit, oldU);
        if (assign) {
          context->assignMask[limb] |= mask;
          context->assignValue[limb] = nextV
                                           ? context->assignValue[limb] | mask
                                           : context->assignValue[limb] & ~mask;
          context->assignUnknown[limb] =
              nextU ? context->assignUnknown[limb] | mask
                    : context->assignUnknown[limb] & ~mask;
          if (limb < context->forceMask.size() &&
              (context->forceMask[limb] & mask) != 0) {
            nextV = oldV;
            nextU = oldU;
          }
        } else {
          context->forceMask[limb] |= mask;
        }
        context->stateValue[limb] = nextV ? context->stateValue[limb] | mask
                                          : context->stateValue[limb] & ~mask;
        context->stateUnknown[limb] = nextU
                                          ? context->stateUnknown[limb] | mask
                                          : context->stateUnknown[limb] & ~mask;
        setByteBit(globalValue, destination, nextV);
        setByteBit(globalUnknown, destination, nextU);
        setByteBit(publishedValue.data(), bit, nextV);
        setByteBit(publishedUnknown.data(), bit, nextU);
      }
      obelisk_rt_aot_external_write_range_unlocked(context, absolute, bitWidth,
                                                   true);
    }
    obelisk_rt_v1_scheduler_signal_transition(
        context, handle, bitWidth, oldValue.data(), oldUnknown.data(),
        publishedValue.data(), publishedUnknown.data());
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_release_override(
    obelisk_rt_context *context, uint8_t *globalValue, uint8_t *globalUnknown,
    uint64_t globalBitCount, uint64_t handle, uint64_t bitWidth,
    uint32_t descriptorKind, uint32_t assign) {
  if (!context || !globalValue || !globalUnknown || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || assign > 1 ||
      (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
       descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
      (assign && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
      !context->execution ||
      context->execution->state_bit_count != globalBitCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    uint64_t absolute = 0;
    uint64_t byteCount = (bitWidth + 7) / 8;
    if (byteCount > std::numeric_limits<size_t>::max())
      return OBELISK_RT_INVALID_ARGUMENT;
    if (descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
      {
        ContextMutexLock lock(context);
        if (!staticOverrideRange(context, handle, bitWidth, absolute))
          return OBELISK_RT_INVALID_HANDLE;
      }
      return obelisk_rt_release_design_nets(context, absolute, bitWidth);
    }
    std::vector<uint8_t> oldValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> oldUnknown(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedValue(static_cast<size_t>(byteCount), 0);
    std::vector<uint8_t> publishedUnknown(static_cast<size_t>(byteCount), 0);
    bool changed = false;
    {
      ContextMutexLock lock(context);
      if (!staticOverrideRange(context, handle, bitWidth, absolute))
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t destination = absolute + bit;
        uint64_t mask = uint64_t{1} << (destination % 64);
        uint64_t limb = destination / 64;
        bool oldV = (context->stateValue[limb] & mask) != 0;
        bool oldU = (context->stateUnknown[limb] & mask) != 0;
        setByteBit(oldValue.data(), bit, oldV);
        setByteBit(oldUnknown.data(), bit, oldU);
        bool nextV = oldV, nextU = oldU;
        if (assign) {
          if (limb < context->assignMask.size())
            context->assignMask[limb] &= ~mask;
        } else {
          if (limb < context->forceMask.size())
            context->forceMask[limb] &= ~mask;
          if (limb < context->assignMask.size() &&
              (context->assignMask[limb] & mask) != 0) {
            nextV = (context->assignValue[limb] & mask) != 0;
            nextU = (context->assignUnknown[limb] & mask) != 0;
            context->stateValue[limb] = nextV
                                            ? context->stateValue[limb] | mask
                                            : context->stateValue[limb] & ~mask;
            context->stateUnknown[limb] =
                nextU ? context->stateUnknown[limb] | mask
                      : context->stateUnknown[limb] & ~mask;
            setByteBit(globalValue, destination, nextV);
            setByteBit(globalUnknown, destination, nextU);
            changed |= oldV != nextV || oldU != nextU;
          }
        }
        setByteBit(publishedValue.data(), bit, nextV);
        setByteBit(publishedUnknown.data(), bit, nextU);
      }
      obelisk_rt_aot_release_range_unlocked(context, absolute, bitWidth);
    }
    if (changed)
      obelisk_rt_v1_scheduler_signal_transition(
          context, handle, bitWidth, oldValue.data(), oldUnknown.data(),
          publishedValue.data(), publishedUnknown.data());
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
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

namespace {

bool managedRootWordBelongsTo(obelisk_rt_context *context,
                              obelisk_rt_managed_word_v1 word) {
  if (word == 0)
    return true;
  if ((word & UINT64_C(3)) == UINT64_C(1))
    return obelisk_rt_validate_string(context, word) == OBELISK_RT_OK;
  if ((word & UINT64_C(3)) != 0)
    return false;
  return obelisk_rt_managed_object_belongs_to(
      context,
      reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word)));
}

obelisk_rt_status publishNativeAutomaticState(obelisk_rt_context *context,
                                              NativeAutomaticState state,
                                              uint64_t *outHandle) {
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    state.owner = context->activeNativeProcess;
    if (!state.owner)
      state.designOwner = context->activeDesignTaskID;
    uint32_t id = context->nextNativeAutomaticID;
    if (id == 0 || id > OBELISK_RT_STABLE_HANDLE_MAX_AUTOMATIC_ID) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (!context->nativeAutomaticStates.emplace(id, std::move(state)).second)
      return OBELISK_RT_INVALID_LIFECYCLE;
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

} // namespace

obelisk_rt_status
obelisk_rt_native_state_alloc_managed(obelisk_rt_context *context,
                                      obelisk_rt_object_v1 *value,
                                      uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!obelisk_rt_managed_object_belongs_to(context, value))
    return OBELISK_RT_INVALID_HANDLE;
  NativeAutomaticState state;
  state.bitWidth = 64;
  state.managedValue = value;
  state.managedRootRegistered = true;
  return publishNativeAutomaticState(context, std::move(state), outHandle);
}

obelisk_rt_status obelisk_rt_native_state_alloc_with_root_offsets(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, std::vector<uint64_t> bitOffsets,
    uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!context || !value || bitWidth == 0 || bitWidth > INT32_MAX ||
      bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    for (size_t index = 0; index != bitOffsets.size(); ++index) {
      uint64_t bitOffset = bitOffsets[index];
      if ((bitOffset & 63) != 0 || bitOffset > bitWidth ||
          64 > bitWidth - bitOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      bitOffsets[index] = bitOffset / 8;
    }
    std::sort(bitOffsets.begin(), bitOffsets.end());
    if (std::adjacent_find(bitOffsets.begin(), bitOffsets.end()) !=
        bitOffsets.end())
      return OBELISK_RT_INVALID_ARGUMENT;

    NativeAutomaticState state;
    state.bitWidth = bitWidth;
    for (uint64_t byteOffset : bitOffsets) {
      obelisk_rt_managed_word_v1 managed = 0;
      std::memcpy(&managed, value + byteOffset, sizeof(managed));
      if (!managedRootWordBelongsTo(context, managed))
        return OBELISK_RT_INVALID_HANDLE;
    }
    state.managedRootByteOffsets = std::move(bitOffsets);
    state.value.assign(value, value + static_cast<size_t>(byteCount));
    if (unknown)
      state.unknown.assign(unknown, unknown + static_cast<size_t>(byteCount));
    return publishNativeAutomaticState(context, std::move(state), outHandle);
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_alloc(obelisk_rt_context *context, uint64_t bitWidth,
                                 const uint8_t *value, const uint8_t *unknown,
                                 uint64_t *outHandle) {
  return obelisk_rt_native_state_alloc_with_root_offsets(
      context, bitWidth, value, unknown, {}, outHandle);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_retain(obelisk_rt_context *context,
                                  uint64_t handle) {
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
    ContextMutexLock lock(context);
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

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_register_managed_roots(
    obelisk_rt_context *context, uint64_t handle, const uint64_t *bitOffsets,
    uint64_t count) {
  if (!context || !bitOffsets || count == 0 ||
      count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  uint32_t id = 0;
  int64_t handleOffset = 0;
  if (!decodeNativeAutomatic(handle, id, handleOffset) || handleOffset != 0)
    return OBELISK_RT_INVALID_HANDLE;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end())
      return OBELISK_RT_INVALID_HANDLE;
    NativeAutomaticState &state = found->second;
    if (state.managedRootRegistered || !state.managedRootByteOffsets.empty())
      return OBELISK_RT_INVALID_LIFECYCLE;
    std::vector<uint64_t> byteOffsets;
    byteOffsets.reserve(static_cast<size_t>(count));
    for (uint64_t index = 0; index != count; ++index) {
      uint64_t bitOffset = bitOffsets[index];
      if ((bitOffset & 63) != 0 || bitOffset > state.bitWidth ||
          64 > state.bitWidth - bitOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      uint64_t byteOffset = bitOffset / 8;
      if (byteOffset > state.value.size() ||
          sizeof(obelisk_rt_managed_word_v1) > state.value.size() - byteOffset)
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, state.value.data() + byteOffset, sizeof(word));
      if (!managedRootWordBelongsTo(context, word))
        return OBELISK_RT_INVALID_HANDLE;
      byteOffsets.push_back(byteOffset);
    }
    std::sort(byteOffsets.begin(), byteOffsets.end());
    if (std::adjacent_find(byteOffsets.begin(), byteOffsets.end()) !=
        byteOffsets.end())
      return OBELISK_RT_INVALID_ARGUMENT;
    state.managedRootByteOffsets = std::move(byteOffsets);
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_native_state_alloc_with_roots(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, const uint64_t *bitOffsets, uint64_t count,
    uint64_t *outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = UINT64_MAX;
  if (!bitOffsets || count == 0 || count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::vector<uint64_t> offsets(bitOffsets, bitOffsets + count);
    return obelisk_rt_native_state_alloc_with_root_offsets(
        context, bitWidth, value, unknown, std::move(offsets), outHandle);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_native_state_release(obelisk_rt_context *context, uint64_t handle,
                                   uint32_t ownerReference) {
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
    ContextMutexLock lock(context);
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end() ||
        found->second.referenceCount == 0)
      return OBELISK_RT_INVALID_HANDLE;
    if (ownerReference) {
      bool nativeOwner = found->second.owner &&
                         found->second.owner == context->activeNativeProcess;
      bool designOwner =
          found->second.designOwner != 0 &&
          found->second.designOwner == context->activeDesignTaskID;
      if (!nativeOwner && !designOwner)
        return OBELISK_RT_INVALID_LIFECYCLE;
      found->second.owner = nullptr;
      found->second.designOwner = 0;
    }
    if (--found->second.referenceCount == 0) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, id);
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
    ContextMutexLock lock(context);
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      const NativeAutomaticState &state = found->second;
      if (state.managedRootRegistered) {
        if (unknownPlane != 0 || offset != 0 || bitWidth != 64)
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(outValue, &state.managedValue, sizeof(state.managedValue));
        return OBELISK_RT_OK;
      }
      const std::vector<uint8_t> &plane =
          unknownPlane ? state.unknown : state.value;
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
    uint64_t rootWidth = 0;
    int64_t globalOffset = 0;
    obelisk_rt_status rangeStatus = resolveCheckedNativePackedRangeUnlocked(
        context, handle, globalBitCount, rootOffset, rootWidth, globalOffset);
    if (rangeStatus != OBELISK_RT_OK) {
      context->schedulerStatus = rangeStatus;
      return rangeStatus;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    const std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane =
          unknownPlane ? &context->stateUnknown : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (canonical && bitWidth <= 64 && globalOffset >= 0 &&
        static_cast<uint64_t>(globalOffset) <= rootWidth &&
        bitWidth <= rootWidth - static_cast<uint64_t>(globalOffset)) {
      uint64_t source = rootOffset + static_cast<uint64_t>(globalOffset);
      uint64_t value = isStaticControlAOT(context)
                           ? loadPackedBytes(globalPlane, source, bitWidth)
                           : loadPackedBits(*canonicalPlane, source, bitWidth);
      for (uint64_t byte = 0; byte != byteCount; ++byte)
        outValue[byte] = static_cast<uint8_t>(value >> (byte * 8));
      maskPadding();
      return OBELISK_RT_OK;
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (addHandleOffset(globalOffset, bit, coordinate) && coordinate >= 0 &&
          static_cast<uint64_t>(coordinate) < rootWidth) {
        uint64_t source = rootOffset + static_cast<uint64_t>(coordinate);
        setByteBit(
            outValue, bit,
            canonical
                ? (((*canonicalPlane)[source / 64] >> (source % 64)) & 1) != 0
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
    obelisk_rt_context *context, uint8_t *globalPlane, uint64_t globalBitCount,
    uint64_t handle, uint64_t bitWidth, uint32_t unknownPlane,
    const uint8_t *value, uint8_t *outChanged) {
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
    ContextMutexLock lock(context);
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return context->schedulerStatus;
    }
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeNativeAutomatic(handle, id, offset)) {
      auto found = context->nativeAutomaticStates.find(id);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return OBELISK_RT_INVALID_HANDLE;
      }
      NativeAutomaticState &state = found->second;
      if (state.managedRootRegistered) {
        if (unknownPlane != 0 || offset != 0 || bitWidth != 64)
          return OBELISK_RT_INVALID_HANDLE;
        obelisk_rt_object_v1 *managed = nullptr;
        std::memcpy(&managed, value, sizeof(managed));
        if (!obelisk_rt_managed_object_belongs_to(context, managed))
          return OBELISK_RT_INVALID_HANDLE;
        *outChanged = managed != state.managedValue;
        state.managedValue = managed;
        return OBELISK_RT_OK;
      }
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
    uint64_t rootWidth = 0;
    int64_t globalOffset = 0;
    obelisk_rt_status rangeStatus = resolveCheckedNativePackedRangeUnlocked(
        context, handle, globalBitCount, rootOffset, rootWidth, globalOffset);
    if (rangeStatus != OBELISK_RT_OK) {
      context->schedulerStatus = rangeStatus;
      return rangeStatus;
    }
    bool canonical = context->execution &&
                     context->execution->state_bit_count == globalBitCount;
    std::vector<uint64_t> *canonicalPlane = nullptr;
    if (canonical) {
      canonicalPlane =
          unknownPlane ? &context->stateUnknown : &context->stateValue;
      if (canonicalPlane->size() != (globalBitCount + 63) / 64)
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (canonical && bitWidth <= 64 && globalOffset >= 0 &&
        static_cast<uint64_t>(globalOffset) <= rootWidth &&
        bitWidth <= rootWidth - static_cast<uint64_t>(globalOffset)) {
      uint64_t destination = rootOffset + static_cast<uint64_t>(globalOffset);
      bool masked = false;
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t absolute = destination + bit;
        uint64_t mask = uint64_t{1} << (absolute % 64);
        masked |= (absolute / 64 < context->forceMask.size() &&
                   (context->forceMask[absolute / 64] & mask) != 0) ||
                  (absolute / 64 < context->assignMask.size() &&
                   (context->assignMask[absolute / 64] & mask) != 0);
      }
      if (!masked) {
        uint64_t next = 0;
        for (uint64_t byte = 0; byte != byteCount; ++byte)
          next |= uint64_t{value[byte]} << (byte * 8);
        next &= packedWidthMask(bitWidth);
        uint64_t old =
            isStaticControlAOT(context)
                ? loadPackedBytes(globalPlane, destination, bitWidth)
                : loadPackedBits(*canonicalPlane, destination, bitWidth);
        *outChanged = old != next;
        storePackedBytes(globalPlane, destination, bitWidth, next);
        storePackedBits(*canonicalPlane, destination, bitWidth, next);
        return OBELISK_RT_OK;
      }
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      int64_t coordinate = 0;
      if (!addHandleOffset(globalOffset, bit, coordinate) || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= rootWidth)
        continue;
      uint64_t destination = rootOffset + static_cast<uint64_t>(coordinate);
      uint64_t destinationMask = uint64_t{1} << (destination % 64);
      bool forced =
          destination / 64 < context->forceMask.size() &&
          (context->forceMask[destination / 64] & destinationMask) != 0;
      bool assigned =
          destination / 64 < context->assignMask.size() &&
          (context->assignMask[destination / 64] & destinationMask) != 0;
      if (canonical && (forced || assigned))
        continue;
      bool old =
          canonical
              ? (((*canonicalPlane)[destination / 64] >> (destination % 64)) &
                 1) != 0
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

extern "C" obelisk_rt_status obelisk_rt_v1_argument_ref_load(
    obelisk_rt_context *context, const uint8_t *stateValue,
    const uint8_t *stateUnknown, uint64_t stateBitCount,
    obelisk_rt_object_v1 *owner, uint64_t payload, uint32_t managed,
    uint64_t bitWidth, uint64_t planeSize, uint32_t fourState,
    uint32_t valueKind, void *outValue, void *outUnknown) {
  if (!context || !stateValue || !stateUnknown || !outValue || bitWidth == 0 ||
      planeSize == 0 || planeSize > std::numeric_limits<size_t>::max() ||
      managed > 2 || fourState > 1 ||
      valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING ||
      (fourState && !outUnknown) ||
      (valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS && fourState))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(planeSize));
  if (outUnknown)
    std::memset(outUnknown, 0, static_cast<size_t>(planeSize));
  if (managed == 2) {
    obelisk_rt_status shape = obelisk_rt_reference_path_shape(
        owner, planeSize, bitWidth, fourState,
        valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS);
    if (shape != OBELISK_RT_OK)
      return shape;
    uint32_t present = 0;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_load(
        owner, outValue, outUnknown, &present);
    if (status != OBELISK_RT_OK ||
        valueKind != OBELISK_RT_ARGUMENT_VALUE_STRING)
      return status;
    obelisk_rt_string_v1 string = 0;
    std::memcpy(&string, outValue, sizeof(string));
    return obelisk_rt_validate_string(context, string);
  }
  if (managed == 1) {
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) {
      if (bitWidth != sizeof(void *) * 8 || planeSize != sizeof(void *))
        return OBELISK_RT_INVALID_ARGUMENT;
      return obelisk_rt_v1_object_field_load(
          owner, payload, static_cast<obelisk_rt_object_v1 **>(outValue));
    }
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_string_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_status status =
          obelisk_rt_v1_object_read(owner, payload, outValue, planeSize);
      if (status != OBELISK_RT_OK)
        return status;
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, outValue, sizeof(string));
      return obelisk_rt_validate_string(context, string);
    }
    if (fourState && payload > UINT64_MAX - planeSize)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (fourState)
      return obelisk_rt_v1_object_read_planes(owner, payload, outValue,
                                              outUnknown, planeSize);
    return obelisk_rt_v1_object_read(owner, payload, outValue, planeSize);
  }
  ContextTransaction transaction(context);
  obelisk_rt_status status = obelisk_rt_v1_native_state_load_plane(
      context, stateValue, stateBitCount, payload, bitWidth, 0, 0,
      static_cast<uint8_t *>(outValue));
  if (status != OBELISK_RT_OK)
    return status;
  if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
    if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
        planeSize != sizeof(obelisk_rt_string_v1))
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_string_v1 string = 0;
    std::memcpy(&string, outValue, sizeof(string));
    return obelisk_rt_validate_string(context, string);
  }
  if (!fourState)
    return OBELISK_RT_OK;
  return obelisk_rt_v1_native_state_load_plane(
      context, stateUnknown, stateBitCount, payload, bitWidth, 1, 1,
      static_cast<uint8_t *>(outUnknown));
}

extern "C" obelisk_rt_status obelisk_rt_v1_argument_ref_store(
    obelisk_rt_context *context, uint8_t *stateValue, uint8_t *stateUnknown,
    uint64_t stateBitCount, obelisk_rt_object_v1 *owner, uint64_t payload,
    uint32_t managed, uint64_t bitWidth, uint64_t planeSize, uint32_t fourState,
    uint32_t valueKind, const void *value, const void *unknown) {
  if (!context || !stateValue || !stateUnknown || !value || bitWidth == 0 ||
      planeSize == 0 || planeSize > std::numeric_limits<size_t>::max() ||
      managed > 2 || fourState > 1 ||
      valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING || (fourState && !unknown) ||
      (valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS && fourState))
    return OBELISK_RT_INVALID_ARGUMENT;
  {
    ContextMutexLock lock(context);
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
  }
  if (managed == 2) {
    obelisk_rt_status shape = obelisk_rt_reference_path_shape(
        owner, planeSize, bitWidth, fourState,
        valueKind != OBELISK_RT_ARGUMENT_VALUE_BITS);
    if (shape != OBELISK_RT_OK)
      return shape;
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, value, sizeof(string));
      obelisk_rt_status status = obelisk_rt_validate_string(context, string);
      if (status != OBELISK_RT_OK)
        return status;
    }
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    return obelisk_rt_v1_reference_path_store(lane, owner, value, unknown);
  }
  if (managed == 1) {
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) {
      if (bitWidth != sizeof(void *) * 8 || planeSize != sizeof(void *))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_object_v1 *stored = nullptr;
      std::memcpy(&stored, value, sizeof(stored));
      return obelisk_rt_v1_object_field_store(owner, payload, stored);
    }
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      if (bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
          planeSize != sizeof(obelisk_rt_string_v1))
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_string_v1 string = 0;
      std::memcpy(&string, value, sizeof(string));
      obelisk_rt_status status = obelisk_rt_validate_string(context, string);
      if (status != OBELISK_RT_OK)
        return status;
      return obelisk_rt_v1_object_write(owner, payload, value, planeSize);
    }
    if (fourState && payload > UINT64_MAX - planeSize)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (fourState)
      return obelisk_rt_v1_object_write_planes(owner, payload, value, unknown,
                                               planeSize);
    return obelisk_rt_v1_object_write(owner, payload, value, planeSize);
  }
  // Keep one reusable transition snapshot per host thread. This avoids a heap
  // allocation for each language `ref` write while preserving exact signal
  // change and edge notification for ordinary simulation storage.
  struct TransitionScratch {
    std::vector<uint8_t> value;
    std::vector<uint8_t> unknown;
  };
  thread_local TransitionScratch scratch;
  ContextTransaction transaction(context);
  try {
    scratch.value.resize(static_cast<size_t>(planeSize));
    if (fourState)
      scratch.unknown.resize(static_cast<size_t>(planeSize));
    ContextMutexLock lock(context);
    obelisk_rt_status status = obelisk_rt_v1_argument_ref_load(
        context, stateValue, stateUnknown, stateBitCount, nullptr, payload, 0,
        bitWidth, planeSize, fourState, valueKind, scratch.value.data(),
        fourState ? scratch.unknown.data() : nullptr);
    if (status != OBELISK_RT_OK)
      return status;
    bool equalStringContents = false;
    if (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) {
      obelisk_rt_string_v1 previous = 0;
      obelisk_rt_string_v1 next = 0;
      std::memcpy(&previous, scratch.value.data(), sizeof(previous));
      std::memcpy(&next, value, sizeof(next));
      status = obelisk_rt_validate_string(context, next);
      if (status != OBELISK_RT_OK)
        return status;
      equalStringContents = obelisk_rt_v1_string_compare(previous, next) == 0;
    }
    uint8_t changed = 0;
    status = obelisk_rt_v1_native_state_store_plane(
        context, stateValue, stateBitCount, payload, bitWidth, 0,
        static_cast<const uint8_t *>(value), &changed);
    if (status != OBELISK_RT_OK)
      return status;
    if (fourState) {
      status = obelisk_rt_v1_native_state_store_plane(
          context, stateUnknown, stateBitCount, payload, bitWidth, 1,
          static_cast<const uint8_t *>(unknown), &changed);
      if (status != OBELISK_RT_OK)
        return status;
    }
    if (!equalStringContents)
      obelisk_rt_v1_scheduler_signal_transition(
          context, payload, bitWidth, scratch.value.data(),
          fourState ? scratch.unknown.data() : nullptr,
          static_cast<const uint8_t *>(value),
          fourState ? static_cast<const uint8_t *>(unknown) : nullptr);
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
    ContextMutexLock lock(context);
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
    ContextMutexLock lock(context);
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
obelisk_rt_v1_scheduler_fatal(obelisk_rt_context *context, uint32_t verbosity) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
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
    ContextMutexLock lock(context);
    return context->schedulerFinishRequested ? 1u : 0u;
  } catch (...) {
    return 0;
  }
}

extern "C" uint64_t obelisk_rt_v1_scheduler_time(obelisk_rt_context *context) {
  if (!context)
    return 0;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    return context->schedulerTime;
  } catch (...) {
    return 0;
  }
}

// Preponed sampling is a once-per-time-slot service. There are no executable
// samplers yet, but keeping the hook exact now prevents the assertion and
// clocking milestones from having to rediscover the initial-time-zero edge.
static obelisk_rt_status runPreponedHooks(obelisk_rt_context *) {
  return OBELISK_RT_OK;
}

#if defined(__x86_64__) || defined(_M_X64)
__attribute__((target("avx2"))) bool commitStaticNBA256AVX2(
    uint64_t *stagedValueWords, uint64_t *stagedUnknownWords,
    uint64_t *writeMaskWords, uint64_t *changedWords, uint64_t *posedgeWords,
    uint64_t *negedgeWords, uint64_t planeBit, uint64_t planeBitCount,
    uint8_t *workingValue, uint8_t *workingUnknown, uint64_t *canonicalValue,
    uint64_t *canonicalUnknown, bool synchronizeCanonical,
    bool trackTransitions, bool updateStagedValues) {
  size_t wordOffset = static_cast<size_t>(planeBit / 64);
  unsigned shift = static_cast<unsigned>(planeBit % 64);
  size_t byteOffset = wordOffset * sizeof(uint64_t);
  size_t planeBytes = static_cast<size_t>((planeBitCount + 7) / 8);
  size_t segmentBytes =
      std::min((shift == 0 ? size_t{4} : size_t{5}) * sizeof(uint64_t),
               planeBytes - byteOffset);
  alignas(32) uint64_t workingValueWords[5] = {};
  alignas(32) uint64_t workingUnknownWords[5] = {};
  std::memcpy(workingValueWords, workingValue + byteOffset, segmentBytes);
  std::memcpy(workingUnknownWords, workingUnknown + byteOffset, segmentBytes);
  alignas(32) uint64_t oldValueWords[4];
  alignas(32) uint64_t oldUnknownWords[4];
  auto extractRoot = [&](const uint64_t *segment, uint64_t *root) {
    if (shift == 0) {
      std::memcpy(root, segment, sizeof(oldValueWords));
      return;
    }
    for (unsigned word = 0; word != 4; ++word)
      root[word] =
          (segment[word] >> shift) | (segment[word + 1] << (64 - shift));
  };
  extractRoot(workingValueWords, oldValueWords);
  extractRoot(workingUnknownWords, oldUnknownWords);
  __m256i writeMask =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(writeMaskWords));
  __m256i oldValue =
      _mm256_load_si256(reinterpret_cast<const __m256i *>(oldValueWords));
  __m256i oldUnknown =
      _mm256_load_si256(reinterpret_cast<const __m256i *>(oldUnknownWords));
  __m256i stagedValue =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(stagedValueWords));
  __m256i stagedUnknown =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(stagedUnknownWords));
  __m256i newValue = _mm256_or_si256(_mm256_andnot_si256(writeMask, oldValue),
                                     _mm256_and_si256(writeMask, stagedValue));
  __m256i newUnknown =
      _mm256_or_si256(_mm256_andnot_si256(writeMask, oldUnknown),
                      _mm256_and_si256(writeMask, stagedUnknown));
  __m256i changed = _mm256_or_si256(_mm256_xor_si256(oldValue, newValue),
                                    _mm256_xor_si256(oldUnknown, newUnknown));
  if (updateStagedValues) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(stagedValueWords),
                        newValue);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(stagedUnknownWords),
                        newUnknown);
  }
  if (trackTransitions) {
    __m256i oldZero = _mm256_andnot_si256(_mm256_or_si256(oldUnknown, oldValue),
                                          _mm256_set1_epi64x(-1));
    __m256i oldOne = _mm256_andnot_si256(oldUnknown, oldValue);
    __m256i newZero = _mm256_andnot_si256(_mm256_or_si256(newUnknown, newValue),
                                          _mm256_set1_epi64x(-1));
    __m256i newOne = _mm256_andnot_si256(newUnknown, newValue);
    __m256i posedge = _mm256_or_si256(_mm256_andnot_si256(newZero, oldZero),
                                      _mm256_and_si256(oldUnknown, newOne));
    __m256i negedge = _mm256_or_si256(_mm256_andnot_si256(newOne, oldOne),
                                      _mm256_and_si256(oldUnknown, newZero));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(changedWords), changed);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(posedgeWords), posedge);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(negedgeWords), negedge);
  }
  alignas(32) uint64_t newValueWords[4];
  alignas(32) uint64_t newUnknownWords[4];
  _mm256_store_si256(reinterpret_cast<__m256i *>(newValueWords), newValue);
  _mm256_store_si256(reinterpret_cast<__m256i *>(newUnknownWords), newUnknown);
  auto mergeRoot = [&](uint64_t *segment, const uint64_t *rootWords) {
    if (shift == 0) {
      std::memcpy(segment, rootWords, 4 * sizeof(*rootWords));
    } else {
      uint64_t lowMask = (uint64_t{1} << shift) - 1;
      segment[0] = (segment[0] & lowMask) | (rootWords[0] << shift);
      for (unsigned word = 1; word != 4; ++word)
        segment[word] =
            (rootWords[word - 1] >> (64 - shift)) | (rootWords[word] << shift);
      segment[4] = (segment[4] & ~lowMask) | (rootWords[3] >> (64 - shift));
    }
  };
  mergeRoot(workingValueWords, newValueWords);
  mergeRoot(workingUnknownWords, newUnknownWords);
  std::memcpy(workingValue + byteOffset, workingValueWords, segmentBytes);
  std::memcpy(workingUnknown + byteOffset, workingUnknownWords, segmentBytes);
  if (synchronizeCanonical) {
    mergeRoot(canonicalValue + wordOffset, newValueWords);
    mergeRoot(canonicalUnknown + wordOffset, newUnknownWords);
  }
  return !_mm256_testz_si256(changed, changed);
}
#endif

obelisk_rt_status tryCommitGeneratedNBA256Unlocked(obelisk_rt_context *context,
                                                   uint32_t rootIndex,
                                                   uint32_t barrierRegion,
                                                   bool &changed,
                                                   bool &handled) {
  handled = false;
#if defined(__x86_64__) || defined(_M_X64)
  if (!context->nativeScheduleAVX2 ||
      rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->staticNBASlowRoots.size())
    return OBELISK_RT_OK;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if (!generated || !hasGeneratedNBAStages(*generated) || accumulator.valid ||
      context->staticNBASlowRoots[rootIndex] != 0 || root.bit_width != 256 ||
      nativeStaticRootDirty(context, root.static_state) ||
      generated->exec_region != barrierRegion ||
      staticNBARootNeedsTransitions(context, rootIndex))
    return OBELISK_RT_OK;
  std::optional<uint64_t> stageCount = countGeneratedNBAStages(*generated);
  if (!stageCount)
    return OBELISK_RT_OK;
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  bool synchronizeCanonical =
      activeNativeAOTContext != context || !canUseStaticAOTFanout(context);
  bool canonical = plan && context->execution &&
                   context->execution->state_bit_count == plan->state_bit_count;
  if (!staticState || staticState->bitWidth != root.bit_width || !canonical ||
      staticState->bitOffset > plan->state_bit_count ||
      root.bit_width > plan->state_bit_count - staticState->bitOffset ||
      (synchronizeCanonical &&
       (context->stateValue.size() != (plan->state_bit_count + 63) / 64 ||
        context->stateUnknown.size() != context->stateValue.size())))
    return OBELISK_RT_OK;
  auto loadOverrideMask = [&](const std::vector<uint64_t> &plane,
                              uint64_t offset, uint64_t width) {
    return plane.empty() ? uint64_t{0}
                         : loadPackedBytes(
                               reinterpret_cast<const uint8_t *>(plane.data()),
                               offset, width);
  };
  if (!context->forceMask.empty() || !context->assignMask.empty())
    for (uint64_t local = 0; local < root.bit_width; local += 64) {
      uint64_t planeBit = staticState->bitOffset + local;
      if ((loadOverrideMask(context->forceMask, planeBit, 64) |
           loadOverrideMask(context->assignMask, planeBit, 64)) != 0)
        return OBELISK_RT_OK;
    }
  if (context->nextSchedulerSequence == 0)
    return OBELISK_RT_OUT_OF_RESOURCES;
  ++context->nextSchedulerSequence;
  bool rootChanged = commitStaticNBA256AVX2(
      generated->value, generated->unknown, generated->write_mask, nullptr,
      nullptr, nullptr, staticState->bitOffset, plan->state_bit_count,
      plan->state_value, plan->state_unknown, context->stateValue.data(),
      context->stateUnknown.data(), synchronizeCanonical, false, false);
  changed |= rootChanged;
  context->signalDiagnostics.aotNBAStages += *stageCount;
  std::fill(std::begin(generated->write_mask), std::end(generated->write_mask),
            uint64_t{0});
  generated->valid = 0;
  ++context->signalDiagnostics.aotNBACommits;
  handled = true;
#else
  (void)context;
  (void)rootIndex;
  (void)barrierRegion;
  (void)changed;
#endif
  return OBELISK_RT_OK;
}

obelisk_rt_status commitStaticNBARootUnlocked(obelisk_rt_context *context,
                                              uint32_t rootIndex,
                                              uint32_t barrierRegion,
                                              bool &changed,
                                              bool allowGeneratedFast = true) {
  if (!context->nativeSchedulePlan)
    return OBELISK_RT_OK;
  if (rootIndex >= context->staticNBAAccumulators.size() ||
      rootIndex >= context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_DESIGN;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[rootIndex];
  obelisk_rt_generated_nba_accumulator_256 *generated =
      root.generated_accumulator;
  if ((!generated || !hasGeneratedNBAStages(*generated)) &&
      (!accumulator.valid || accumulator.execRegion != barrierRegion))
    return OBELISK_RT_OK;
  if (allowGeneratedFast) {
    bool generatedHandled = false;
    if (obelisk_rt_status status = tryCommitGeneratedNBA256Unlocked(
            context, rootIndex, barrierRegion, changed, generatedHandled);
        status != OBELISK_RT_OK || generatedHandled)
      return status;
  }
  bool trackTransitions = staticNBARootNeedsTransitions(context, rootIndex);
  if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
          context, rootIndex, barrierRegion);
      status != OBELISK_RT_OK)
    return status;
  if (!accumulator.valid || accumulator.execRegion != barrierRegion)
    return OBELISK_RT_OK;
  const NativeStaticState *staticState =
      findNativeStaticState(context, root.static_state);
  if (!staticState || staticState->bitWidth != root.bit_width ||
      staticState->bitOffset > accumulator.planeBitCount ||
      root.bit_width > accumulator.planeBitCount - staticState->bitOffset)
    return OBELISK_RT_LAYOUT_MISMATCH;
  bool canonical = context->execution && context->execution->state_bit_count ==
                                             accumulator.planeBitCount;
  if (canonical &&
      (context->stateValue.size() != (accumulator.planeBitCount + 63) / 64 ||
       context->stateUnknown.size() != context->stateValue.size()))
    return OBELISK_RT_INVALID_DESIGN;
  auto *workingValue = accumulator.valuePlane;
  auto *workingUnknown = accumulator.unknownPlane;
  if (!workingValue)
    return OBELISK_RT_INVALID_DESIGN;
  auto loadMask = [&](const std::vector<uint64_t> &plane, uint64_t offset,
                      uint64_t width) {
    return !canonical || plane.empty()
               ? uint64_t{0}
               : loadPackedBytes(
                     reinterpret_cast<const uint8_t *>(plane.data()), offset,
                     width);
  };
  bool rootChanged = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool rootHasOverride = false;
  for (uint64_t local = 0; local < root.bit_width && !rootHasOverride;
       local += 64) {
    uint64_t width = std::min<uint64_t>(64, root.bit_width - local);
    uint64_t planeBit = staticState->bitOffset + local;
    rootHasOverride = (loadMask(context->forceMask, planeBit, width) |
                       loadMask(context->assignMask, planeBit, width)) != 0;
  }
  bool usedAVX2 =
      root.bit_width == 256 && context->nativeScheduleAVX2 && canonical &&
      !nativeStaticRootDirty(context, root.static_state) && !rootHasOverride &&
      accumulator.value.size() == 4 && accumulator.unknown.size() == 4 &&
      accumulator.writeMask.size() == 4 && accumulator.changed.size() == 4 &&
      accumulator.posedge.size() == 4 && accumulator.negedge.size() == 4;
  if (usedAVX2)
    rootChanged = commitStaticNBA256AVX2(
        accumulator.value.data(), accumulator.unknown.data(),
        accumulator.writeMask.data(), accumulator.changed.data(),
        accumulator.posedge.data(), accumulator.negedge.data(),
        staticState->bitOffset, accumulator.planeBitCount, workingValue,
        workingUnknown ? workingUnknown
                       : context->nativeSchedulePlan->state_unknown,
        context->stateValue.data(), context->stateUnknown.data(), true,
        trackTransitions, true);
  else
#endif
    for (uint64_t local = 0; local < root.bit_width; local += 64) {
      size_t word = static_cast<size_t>(local / 64);
      uint64_t width = std::min<uint64_t>(64, root.bit_width - local);
      uint64_t widthMask = packedWidthMask(width);
      uint64_t planeBit = staticState->bitOffset + local;
      uint64_t writeMask = accumulator.writeMask[word] & widthMask;
      writeMask &= ~(loadMask(context->forceMask, planeBit, width) |
                     loadMask(context->assignMask, planeBit, width));
      uint64_t oldValue = loadPackedBytes(workingValue, planeBit, width);
      uint64_t oldUnknown =
          workingUnknown ? loadPackedBytes(workingUnknown, planeBit, width)
                         : uint64_t{0};
      uint64_t newValue =
          (oldValue & ~writeMask) | (accumulator.value[word] & writeMask);
      uint64_t newUnknown =
          (oldUnknown & ~writeMask) | (accumulator.unknown[word] & writeMask);
      uint64_t changedBits =
          ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;
      if (trackTransitions) {
        uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
        uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
        uint64_t newZero = ~newUnknown & ~newValue & widthMask;
        uint64_t newOne = ~newUnknown & newValue & widthMask;
        accumulator.changed[word] = changedBits;
        accumulator.posedge[word] =
            ((oldZero & ~newZero) | (oldUnknown & newOne)) & widthMask;
        accumulator.negedge[word] =
            ((oldOne & ~newOne) | (oldUnknown & newZero)) & widthMask;
      }
      accumulator.value[word] = newValue;
      accumulator.unknown[word] = newUnknown;
      rootChanged |= changedBits != 0;
      if (accumulator.valuePlane)
        storePackedBytes(accumulator.valuePlane, planeBit, width, newValue);
      if (accumulator.unknownPlane)
        storePackedBytes(accumulator.unknownPlane, planeBit, width, newUnknown);
      const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
      if (plan && plan->state_bit_count == accumulator.planeBitCount) {
        if (plan->state_value != accumulator.valuePlane)
          storePackedBytes(plan->state_value, planeBit, width, newValue);
        if (plan->state_unknown &&
            plan->state_unknown != accumulator.unknownPlane)
          storePackedBytes(plan->state_unknown, planeBit, width, newUnknown);
      }
      if (canonical) {
        storePackedBytes(
            reinterpret_cast<uint8_t *>(context->stateValue.data()), planeBit,
            width, newValue);
        storePackedBytes(
            reinterpret_cast<uint8_t *>(context->stateUnknown.data()), planeBit,
            width, newUnknown);
      }
    }
  if (rootChanged) {
    if (!trackTransitions) {
      changed = true;
    } else {
      uint64_t rootHandle = obelisk_rt_stable_handle_encode(
          OBELISK_RT_STABLE_HANDLE_STATIC, root.static_state, 0);
      uint64_t sequence = 0;
      if (rootHandle == UINT64_MAX ||
          !obelisk_rt_publish_signal_transition_batch_unlocked(
              context, rootHandle, root.bit_width,
              reinterpret_cast<uint8_t *>(accumulator.changed.data()),
              reinterpret_cast<uint8_t *>(accumulator.posedge.data()),
              reinterpret_cast<uint8_t *>(accumulator.negedge.data()), 0,
              &sequence))
        return context->schedulerStatus;
      obelisk_rt_invalidate_signal_snapshots_unlocked(context, rootHandle,
                                                      root.bit_width);
      if (obelisk_rt_has_conditional_signal_waiters(context)) {
        for (uint64_t bit = 0; bit != root.bit_width; ++bit) {
          uint64_t mask = uint64_t{1} << (bit % 64);
          if ((accumulator.changed[bit / 64] & mask) == 0)
            continue;
          uint64_t eventHandle =
              nativeHandleOffset(rootHandle, static_cast<int64_t>(bit));
          context->signalValueSnapshots[eventHandle] = {
              sequence, (accumulator.value[bit / 64] & mask) != 0,
              (accumulator.unknown[bit / 64] & mask) != 0};
          uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
          if ((accumulator.posedge[bit / 64] & mask) != 0)
            edges |= OBELISK_RT_SIGNAL_POSEDGE;
          if ((accumulator.negedge[bit / 64] & mask) != 0)
            edges |= OBELISK_RT_SIGNAL_NEGEDGE;
          if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                  context, eventHandle, edges))
            return context->schedulerStatus;
        }
      }
      if (!obelisk_rt_notify_observer_signal_unlocked(context, rootHandle,
                                                      root.bit_width))
        return context->schedulerStatus;
      changed = true;
    }
  }
  std::fill(accumulator.writeMask.begin(), accumulator.writeMask.end(),
            uint64_t{0});
  accumulator.valid = false;
  accumulator.sequence = 0;
  ++context->signalDiagnostics.aotNBACommits;
  return OBELISK_RT_OK;
}

#if defined(__x86_64__) || defined(_M_X64)
bool tryCommitGeneratedNBA256BatchUnlocked(obelisk_rt_context *context,
                                           uint32_t rootCount,
                                           uint32_t barrierRegion,
                                           bool &changed) {
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (!plan || activeNativeAOTContext != context ||
      !context->nativeScheduleAVX2 || !canUseStaticAOTFanout(context) ||
      context->nativeScheduleDirtyRootsPresent ||
      rootCount > context->nativeScheduleNBARootCount ||
      rootCount > context->staticNBAAccumulators.size() ||
      rootCount > context->staticNBASlowRoots.size() ||
      context->nextSchedulerSequence == 0 ||
      context->nextSchedulerSequence > UINT64_MAX - rootCount)
    return false;

  // Validate the whole batch before mutating any root. If one root needs
  // masking, transitions, or generic staging, the ordinary per-root commit
  // path must preserve source order for the complete barrier.
  for (uint32_t index = 0; index != rootCount; ++index) {
    const obelisk_rt_static_nba_root &root =
        context->nativeScheduleNBARoots[index];
    const StaticNBAAccumulator &accumulator =
        context->staticNBAAccumulators[index];
    const obelisk_rt_generated_nba_accumulator_256 *generated =
        root.generated_accumulator;
    const NativeStaticState *state =
        findNativeStaticState(context, root.static_state);
    if (!generated || !hasGeneratedNBAStages(*generated) ||
        !countGeneratedNBAStages(*generated) || accumulator.valid ||
        context->staticNBASlowRoots[index] != 0 || root.bit_width != 256 ||
        staticNBARootNeedsTransitions(context, index) ||
        generated->exec_region != barrierRegion || !state ||
        state->bitWidth != root.bit_width ||
        state->bitOffset > plan->state_bit_count ||
        root.bit_width > plan->state_bit_count - state->bitOffset)
      return false;
  }

  for (uint32_t index = 0; index != rootCount; ++index) {
    const obelisk_rt_static_nba_root &root =
        context->nativeScheduleNBARoots[index];
    obelisk_rt_generated_nba_accumulator_256 &generated =
        *root.generated_accumulator;
    const NativeStaticState &state =
        *findNativeStaticState(context, root.static_state);
    uint64_t stageCount = *countGeneratedNBAStages(generated);
    ++context->nextSchedulerSequence;
    changed |= commitStaticNBA256AVX2(
        generated.value, generated.unknown, generated.write_mask, nullptr,
        nullptr, nullptr, state.bitOffset, plan->state_bit_count,
        plan->state_value, plan->state_unknown, nullptr, nullptr, false, false,
        false);
    context->signalDiagnostics.aotNBAStages += stageCount;
    generated.write_mask[0] = 0;
    generated.write_mask[1] = 0;
    generated.write_mask[2] = 0;
    generated.write_mask[3] = 0;
    generated.valid = 0;
    ++context->signalDiagnostics.aotNBACommits;
  }
  return true;
}
#endif

obelisk_rt_status commitStaticNBARootRangeUnlocked(obelisk_rt_context *context,
                                                   uint32_t rootCount,
                                                   uint32_t barrierRegion,
                                                   bool &changed) {
#if defined(__x86_64__) || defined(_M_X64)
  if (tryCommitGeneratedNBA256BatchUnlocked(context, rootCount, barrierRegion,
                                            changed))
    return OBELISK_RT_OK;
#endif
  for (uint32_t root = 0; root != rootCount; ++root) {
    bool generatedHandled = false;
    if (obelisk_rt_status status = tryCommitGeneratedNBA256Unlocked(
            context, root, barrierRegion, changed, generatedHandled);
        status != OBELISK_RT_OK)
      return status;
    if (generatedHandled)
      continue;
    if (obelisk_rt_status status = commitStaticNBARootUnlocked(
            context, root, barrierRegion, changed, false);
        status != OBELISK_RT_OK)
      return status;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
commitStaticNBAAccumulatorsUnlocked(obelisk_rt_context *context,
                                    uint32_t barrierRegion, bool &changed) {
  if (!context->nativeSchedulePlan)
    return OBELISK_RT_OK;
  if (context->nativeSchedulePlan->nba_commit) {
    uint32_t callbackChanged = changed ? 1u : 0u;
    obelisk_rt_status status = context->nativeSchedulePlan->nba_commit(
        context->nativeSchedulePlan->mutable_state, context, barrierRegion,
        &callbackChanged);
    changed = callbackChanged != 0;
    return status;
  }
  return commitStaticNBARootRangeUnlocked(
      context, static_cast<uint32_t>(context->staticNBAAccumulators.size()),
      barrierRegion, changed);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_static_nba_commit_root(obelisk_rt_context *context,
                                     uint32_t rootIndex, uint32_t barrierRegion,
                                     uint32_t *outChanged) {
  if (!context || !outChanged || !context->nativeSchedulePlan)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = *outChanged != 0;
  obelisk_rt_status status =
      commitStaticNBARootUnlocked(context, rootIndex, barrierRegion, changed);
  *outChanged = changed ? 1u : 0u;
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_static_nba_commit_roots(
    obelisk_rt_context *context, uint32_t rootCount, uint32_t barrierRegion,
    uint32_t *outChanged) {
  if (!context || !outChanged ||
      rootCount > context->staticNBAAccumulators.size() ||
      rootCount > context->nativeScheduleNBARootCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = *outChanged != 0;
  obelisk_rt_status status = commitStaticNBARootRangeUnlocked(
      context, rootCount, barrierRegion, changed);
  *outChanged = changed ? 1u : 0u;
  return status;
}

bool canCommitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                             uint32_t barrierRegion) {
  if (!context->execution ||
      context->stateValue.size() !=
          (context->execution->state_bit_count + 63) / 64 ||
      context->stateUnknown.size() != context->stateValue.size())
    return false;
  for (const ScheduledNBA &update : context->scheduledNBAs) {
    if (update.dueTime > context->schedulerTime ||
        update.execRegion != barrierRegion)
      continue;
    uint32_t staticID = 0;
    int64_t offset = 0;
    if (!update.inlinePacked || update.stringValue || update.managedValue ||
        update.retainedAutomaticID != 0 || update.bitWidth == 0 ||
        update.bitWidth > 64 ||
        update.planeBitCount != context->execution->state_bit_count ||
        !decodeNativeStatic(update.bitOffset, staticID, offset) || offset < 0)
      return false;
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state || state->bitOffset > update.planeBitCount ||
        state->bitWidth > update.planeBitCount - state->bitOffset ||
        static_cast<uint64_t>(offset) > state->bitWidth ||
        update.bitWidth > state->bitWidth - static_cast<uint64_t>(offset))
      return false;
  }
  return true;
}

obelisk_rt_status
commitInlineNativeNBABarrierUnlocked(obelisk_rt_context *context,
                                     uint32_t barrierRegion, bool &changed) {
  size_t retained = 0;
  for (size_t index = 0; index != context->scheduledNBAs.size(); ++index) {
    ScheduledNBA &update = context->scheduledNBAs[index];
    bool due = update.dueTime <= context->schedulerTime &&
               update.execRegion == barrierRegion;
    if (!due) {
      if (retained != index)
        context->scheduledNBAs[retained] = std::move(update);
      ++retained;
      continue;
    }

    uint32_t staticID = 0;
    int64_t offset = 0;
    if (!decodeNativeStatic(update.bitOffset, staticID, offset) || offset < 0)
      return OBELISK_RT_INVALID_HANDLE;
    const NativeStaticState *state = findNativeStaticState(context, staticID);
    if (!state)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t planeBit = state->bitOffset + static_cast<uint64_t>(offset);
    uint64_t widthMask = packedWidthMask(update.bitWidth);
    auto loadMask = [&](const std::vector<uint64_t> &mask) {
      if (mask.empty())
        return uint64_t{0};
      return loadPackedBytes(reinterpret_cast<const uint8_t *>(mask.data()),
                             planeBit, update.bitWidth);
    };
    uint64_t writableMask = widthMask & ~(loadMask(context->forceMask) |
                                          loadMask(context->assignMask));
    auto *canonicalValue =
        reinterpret_cast<uint8_t *>(context->stateValue.data());
    auto *canonicalUnknown =
        reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    uint64_t oldValue =
        loadPackedBytes(canonicalValue, planeBit, update.bitWidth);
    uint64_t oldUnknown =
        loadPackedBytes(canonicalUnknown, planeBit, update.bitWidth);
    uint64_t newValue =
        (oldValue & ~writableMask) | (update.inlineValue & writableMask);
    uint64_t newUnknown =
        (oldUnknown & ~writableMask) | (update.inlineUnknown & writableMask);
    uint64_t changedBits =
        ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;

    if (update.valuePlane)
      storePackedBytes(update.valuePlane, planeBit, update.bitWidth, newValue);
    if (update.unknownPlane)
      storePackedBytes(update.unknownPlane, planeBit, update.bitWidth,
                       newUnknown);
    storePackedBytes(canonicalValue, planeBit, update.bitWidth, newValue);
    storePackedBytes(canonicalUnknown, planeBit, update.bitWidth, newUnknown);

    if (changedBits == 0)
      continue;
    changed = true;
    uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
    uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
    uint64_t newZero = ~newUnknown & ~newValue & widthMask;
    uint64_t newOne = ~newUnknown & newValue & widthMask;
    uint64_t posedge = (oldZero & ~newZero) | (oldUnknown & newOne);
    uint64_t negedge = (oldOne & ~newOne) | (oldUnknown & newZero);
    PackedSignalTransitionBuffer transitions(update.bitWidth);
    uint64_t byteCount = (update.bitWidth + 7) / 8;
    std::memcpy(transitions.changed(), &changedBits, byteCount);
    std::memcpy(transitions.posedge(), &posedge, byteCount);
    std::memcpy(transitions.negedge(), &negedge, byteCount);
    uint64_t sequence = 0;
    if (!obelisk_rt_publish_signal_transition_batch_unlocked(
            context, update.bitOffset, update.bitWidth, transitions.changed(),
            transitions.posedge(), transitions.negedge(), 0, &sequence))
      return context->schedulerStatus;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, update.bitOffset,
                                                    update.bitWidth);
    if (obelisk_rt_has_conditional_signal_waiters(context)) {
      for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
        if (!byteBit(transitions.changed(), bit) ||
            bit > static_cast<uint64_t>(INT64_MAX))
          continue;
        uint64_t eventHandle =
            nativeHandleOffset(update.bitOffset, static_cast<int64_t>(bit));
        if (eventHandle == UINT64_MAX)
          continue;
        context->signalValueSnapshots[eventHandle] = {
            sequence, ((newValue >> bit) & uint64_t{1}) != 0,
            ((newUnknown >> bit) & uint64_t{1}) != 0};
      }
      for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
        if (!byteBit(transitions.changed(), bit) ||
            bit > static_cast<uint64_t>(INT64_MAX))
          continue;
        uint64_t eventHandle =
            nativeHandleOffset(update.bitOffset, static_cast<int64_t>(bit));
        if (eventHandle == UINT64_MAX)
          continue;
        uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
        if (byteBit(transitions.posedge(), bit))
          edges |= OBELISK_RT_SIGNAL_POSEDGE;
        if (byteBit(transitions.negedge(), bit))
          edges |= OBELISK_RT_SIGNAL_NEGEDGE;
        if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                context, eventHandle, edges))
          return context->schedulerStatus;
      }
    }
    if (!obelisk_rt_notify_observer_signal_unlocked(context, update.bitOffset,
                                                    update.bitWidth))
      return context->schedulerStatus;
  }
  context->scheduledNBAs.resize(retained);
  return OBELISK_RT_OK;
}

obelisk_rt_status runStaticAOTControlStep(obelisk_rt_context *context) {
  ContextMutexLock lock(context);
  if (!context->nativeSchedulePlan ||
      (context->nativeSchedulePlan->flags &
       OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) == 0)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (context->schedulerStatus != OBELISK_RT_OK)
    return context->schedulerStatus;
  if (context->schedulerFinishRequested) {
    for (uint32_t root = 0; root != context->staticNBAAccumulators.size();
         ++root) {
      StaticNBAAccumulator &accumulator = context->staticNBAAccumulators[root];
      std::fill(accumulator.writeMask.begin(), accumulator.writeMask.end(),
                uint64_t{0});
      accumulator.valid = false;
      accumulator.sequence = 0;
      if (context->nativeScheduleNBARoots[root].generated_accumulator)
        *context->nativeScheduleNBARoots[root].generated_accumulator = {};
    }
    context->schedulerRunningFinals = true;
  }
  if (!context->scheduledManagedNBAs.empty() ||
      !context->scheduledDesignNBAs.empty() ||
      !context->scheduledDesignEvents.empty() ||
      !context->scheduledDesignTasks.empty() ||
      context->nativeScheduleExternalWritePending)
    return OBELISK_RT_TIER_UNAVAILABLE;

  uint32_t barrierRegion = UINT32_MAX;
  for (const StaticNBAAccumulator &accumulator : context->staticNBAAccumulators)
    if (accumulator.valid)
      barrierRegion = std::min(barrierRegion, accumulator.execRegion);
  for (uint32_t root = 0; root != context->nativeScheduleNBARootCount; ++root) {
    const obelisk_rt_generated_nba_accumulator_256 *generated =
        context->nativeScheduleNBARoots[root].generated_accumulator;
    if (generated && hasGeneratedNBAStages(*generated))
      barrierRegion = std::min(barrierRegion, generated->exec_region);
  }
  for (const ScheduledNBA &update : context->scheduledNBAs)
    if (update.dueTime <= context->schedulerTime)
      barrierRegion = std::min(barrierRegion, update.execRegion);
  if (barrierRegion != UINT32_MAX) {
    if (!canCommitInlineNativeNBABarrierUnlocked(context, barrierRegion))
      return OBELISK_RT_TIER_UNAVAILABLE;
    bool changed = false;
    obelisk_rt_status status =
        commitStaticNBAAccumulatorsUnlocked(context, barrierRegion, changed);
    if (status != OBELISK_RT_OK)
      return status;
    status =
        commitInlineNativeNBABarrierUnlocked(context, barrierRegion, changed);
    if (status != OBELISK_RT_OK)
      return status;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    ++context->schedulerSlotProgress;
    return OBELISK_RT_OK;
  }

  if (!context->schedulerRunningFinals) {
    if (!context->nativeScheduleDeadlineHeap.empty()) {
      uint32_t slot = context->nativeScheduleDeadlineHeap.front();
      context->schedulerTime = context->nativeScheduleDeadlines[slot];
      if (!markDueNativeAOTDeadlinesUnlocked(context))
        return OBELISK_RT_INVALID_CONTINUATION;
      obelisk_rt_status status = runPreponedHooks(context);
      if (status != OBELISK_RT_OK) {
        context->schedulerStatus = status;
        return status;
      }
      context->schedulerPreponedTime = context->schedulerTime;
      context->schedulerSlotProgress = 0;
      std::fill(context->staticNBASlowRoots.begin(),
                context->staticNBASlowRoots.end(), uint8_t{0});
      refreshNativeStaticSpecializationFastUnlocked(context);
      return OBELISK_RT_OK;
    }
    bool hasFinal = false;
    for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
         ++slot) {
      if (!context->nativeScheduleActors[slot])
        continue;
      size_t index = context->nativeScheduleActorIndices[slot];
      if (index >= context->scheduledProcesses.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      const ScheduledProcess &candidate = context->scheduledProcesses[index];
      hasFinal |= candidate.instance && candidate.phase == 1;
    }
    if (hasFinal) {
      context->schedulerRunningFinals = true;
      return OBELISK_RT_OK;
    }
  }
  return context->schedulerFinishStatus;
}

obelisk_rt_status
adoptScheduledSuspendUnlocked(obelisk_rt_context *context,
                              ScheduledProcess &scheduled,
                              const obelisk_rt_fragment_action_v1 &action) {
  scheduled.suspendKind = action.suspend_kind;
  scheduled.waitOffset = action.payload;
  scheduled.waitSize = action.auxiliary;
  if (!scheduled.continuationRanks.empty()) {
    auto rank = std::lower_bound(
        scheduled.continuationRanks.begin(), scheduled.continuationRanks.end(),
        action.continuation,
        [](const std::pair<uint32_t, uint32_t> &entry, uint32_t continuation) {
          return entry.first < continuation;
        });
    if (rank != scheduled.continuationRanks.end() &&
        rank->first == action.continuation)
      scheduled.scheduleRank = rank->second;
  }
  scheduled.observedEpoch = context->schedulerEpoch;
  scheduled.waitGenerations.clear();
  scheduled.signalTriggered = false;
  scheduled.urgent = false;
  const obelisk_rt_wait_record_v1 *wait = currentWait(scheduled);
  if (!wait && action.suspend_kind != OBELISK_RT_SUSPEND_OBSERVER)
    return OBELISK_RT_INVALID_FRAME;
  uint64_t delayPayload =
      action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
  if (!obelisk_rt_next_queued_region(scheduled.homeRegion, action.suspend_kind,
                                     delayPayload, action.flags,
                                     scheduled.queuedRegion))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT) {
    const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
    scheduled.waitGenerations.reserve(wait->count);
    for (uint32_t index = 0; index != wait->count; ++index) {
      auto event = context->events.find(entries[index].stable_id);
      scheduled.waitGenerations.push_back(
          event == context->events.end() ? 0 : event->second.generation);
    }
  }
  if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
    scheduled.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + wait->payload;
  if (action.suspend_kind == OBELISK_RT_SUSPEND_OBSERVER &&
      !obelisk_rt_register_computed_signal_wait_unlocked(
          context, computedWait(scheduled), scheduled.token, false,
          scheduled.signalSubscriptions, scheduled.signalLatch))
    return context->schedulerStatus;
  if ((action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
       action.suspend_kind == OBELISK_RT_SUSPEND_EDGE) &&
      !obelisk_rt_register_signal_wait_unlocked(
          context, currentWait(scheduled), scheduled.signalSubscriptions,
          scheduled.signalLatch, scheduled.token, false))
    return context->schedulerStatus;
  return OBELISK_RT_OK;
}

obelisk_rt_status runScheduler(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr uint64_t maxSlotProgress = UINT64_C(1) << 20;
  auto recordSlotProgress = [&]() -> obelisk_rt_status {
    ContextMutexLock lock(context);
    if (context->schedulerSlotProgress == maxSlotProgress) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    ++context->schedulerSlotProgress;
    return OBELISK_RT_OK;
  };
  for (;;) {
    {
      ContextMutexLock lock(context);
      if (context->destroyPending)
        return OBELISK_RT_OK;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.schedulerIterations;
      if (context->schedulerStatus != OBELISK_RT_OK)
        return context->schedulerStatus;
      if (context->schedulerPreponedTime != context->schedulerTime) {
        obelisk_rt_status status = runPreponedHooks(context);
        if (status != OBELISK_RT_OK) {
          context->schedulerStatus = status;
          return status;
        }
        context->schedulerPreponedTime = context->schedulerTime;
        context->schedulerSlotProgress = 0;
        std::fill(context->staticNBASlowRoots.begin(),
                  context->staticNBASlowRoots.end(), uint8_t{0});
        refreshNativeStaticSpecializationFastUnlocked(context);
      }
      // No selected index survives across loop iterations. Reentrant scheduler
      // calls during an evaluator do have an outer selected index, so defer
      // compaction until both execution engines are quiescent.
      if (context->schedulerCompactionPending &&
          !context->activeNativeProcess && !context->designTaskExecuting) {
        auto &processes = context->scheduledProcesses;
        // Compact after roughly one quarter of the vector is dead. This keeps
        // scheduler scans bounded without turning sequential process churn
        // into one vector shift per termination.
        size_t processCompactionThreshold = std::max<size_t>(
            1, processes.size() / 4 + (processes.size() % 4 != 0));
        bool compactProcesses =
            context->schedulerDeadProcessCount == processes.size() ||
            context->schedulerDeadProcessCount >= processCompactionThreshold;
        if (compactProcesses && context->schedulerDeadProcessCount != 0) {
          size_t cursor = processes.empty()
                              ? 0
                              : context->schedulerCursor % processes.size();
          size_t adjustedCursor = static_cast<size_t>(
              std::count_if(processes.begin(), processes.begin() + cursor,
                            [](const ScheduledProcess &process) {
                              return process.instance != nullptr;
                            }));
          processes.erase(std::remove_if(processes.begin(), processes.end(),
                                         [](const ScheduledProcess &process) {
                                           return !process.instance;
                                         }),
                          processes.end());
          context->schedulerCursor =
              processes.empty() ? 0 : adjustedCursor % processes.size();
          context->schedulerDeadProcessCount = 0;
          rebuildNativeSchedulerIndexUnlocked(context);
        }

        auto &tasks = context->scheduledDesignTasks;
        if (context->schedulerDeadDesignTaskCount != 0) {
          for (ScheduledDesignTask &task : tasks)
            if (task.terminated)
              context->designTaskFrames.release(std::move(task.frame));
          tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                     [](const ScheduledDesignTask &task) {
                                       return task.terminated;
                                     }),
                      tasks.end());
          context->schedulerDeadDesignTaskCount = 0;
          context->scheduledDesignTaskIndices.clear();
          context->designPollCandidates.clear();
          context->scheduledDesignTaskIndices.reserve(tasks.size());
          context->designPollCandidates.reserve(tasks.size());
          for (size_t index = 0; index != tasks.size(); ++index) {
            const ScheduledDesignTask &task = tasks[index];
            context->scheduledDesignTaskIndices[task.id] = index;
            if (!obelisk_rt_design_signal_wait_blocked(task))
              context->designPollCandidates.insert(task.id);
          }
        }
        context->schedulerCompactionPending =
            context->schedulerDeadProcessCount != 0;
      }
      if (context->schedulerFinishRequested)
        context->schedulerRunningFinals = true;
    }
    uint32_t nativeRegion = UINT32_MAX;
    uint32_t nativeRank = UINT32_MAX;
    uint64_t nativeInsertionSequence = UINT64_MAX;
    size_t nativeCandidateIndex = SIZE_MAX;
    uint64_t nativeCandidateToken = 0;
    size_t nativeScanProcessCount = 0;
    uint64_t nativeScanEpoch = 0;
    uint64_t nativeScanSelectionGeneration = 0;
    uint64_t nativeScanInsertionSequence = 0;
    uint32_t barrierRegion = UINT32_MAX;
    {
      ContextMutexLock lock(context);
      if (context->scheduledProcessIndices.size() !=
          context->scheduledProcesses.size())
        rebuildNativeSchedulerIndexUnlocked(context);
      nativeScanProcessCount = context->scheduledProcesses.size();
      nativeScanEpoch = context->schedulerEpoch;
      nativeScanSelectionGeneration = context->schedulerSelectionGeneration;
      nativeScanInsertionSequence = context->nextProcessInsertionSequence;
      size_t nativeUrgentDistance = SIZE_MAX;
      bool forcedNativeNode = context->nativeScheduleForcedSlot != UINT32_MAX;
      auto considerNativeToken = [&](uint64_t token) {
        auto indexed = context->scheduledProcessIndices.find(token);
        if (indexed == context->scheduledProcessIndices.end() ||
            indexed->second >= nativeScanProcessCount)
          return;
        size_t index = indexed->second;
        const ScheduledProcess &candidate = context->scheduledProcesses[index];
        if (context->signalDiagnosticsEnabled && !forcedNativeNode)
          ++context->signalDiagnostics.candidateScans;
        if (!candidate.instance ||
            candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
          return;
        bool runnable =
            nativeProcessReady(*context, candidate, forcedNativeNode);
        if (runnable && candidate.urgent) {
          size_t distance =
              nativeScanProcessCount == 0
                  ? 0
                  : (index + nativeScanProcessCount -
                     context->schedulerCursor % nativeScanProcessCount) %
                        nativeScanProcessCount;
          if (distance < nativeUrgentDistance) {
            nativeUrgentDistance = distance;
            nativeRegion = 0;
            nativeRank = 0;
            nativeInsertionSequence = 0;
            nativeCandidateIndex = index;
            nativeCandidateToken = candidate.token;
          }
          return;
        }
        if (nativeUrgentDistance != SIZE_MAX)
          return;
        auto key = std::tuple{candidate.queuedRegion, candidate.scheduleRank,
                              candidate.insertionSequence};
        if (runnable && key < std::tuple{nativeRegion, nativeRank,
                                         nativeInsertionSequence}) {
          nativeRegion = candidate.queuedRegion;
          nativeRank = candidate.scheduleRank;
          nativeInsertionSequence = candidate.insertionSequence;
          nativeCandidateIndex = index;
          nativeCandidateToken = candidate.token;
        }
      };
      if (forcedNativeNode) {
        uint32_t slot = context->nativeScheduleForcedSlot;
        if (slot >= context->nativeScheduleActorTokens.size() ||
            context->nativeScheduleActorTokens[slot] == 0)
          return OBELISK_RT_INVALID_LIFECYCLE;
        considerNativeToken(context->nativeScheduleActorTokens[slot]);
      } else if (!context->nativeScheduleControlOnly) {
        for (uint64_t token : context->nativePollCandidates)
          considerNativeToken(token);
      }
      auto considerBarrier = [&](const auto &entries) {
        for (const auto &entry : entries)
          if (entry.dueTime <= context->schedulerTime)
            barrierRegion = std::min(barrierRegion, entry.execRegion);
      };
      considerBarrier(context->scheduledNBAs);
      considerBarrier(context->scheduledManagedNBAs);
      considerBarrier(context->scheduledDesignNBAs);
      considerBarrier(context->scheduledDesignEvents);
      for (const StaticNBAAccumulator &accumulator :
           context->staticNBAAccumulators)
        if (accumulator.valid)
          barrierRegion = std::min(barrierRegion, accumulator.execRegion);
      for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
           ++root) {
        const obelisk_rt_generated_nba_accumulator_256 *generated =
            context->nativeScheduleNBARoots[root].generated_accumulator;
        if (generated && hasGeneratedNBAStages(*generated))
          barrierRegion = std::min(barrierRegion, generated->exec_region);
      }
    }
    uint32_t maximumRegion = nativeRegion;
    uint32_t maximumRank = nativeRank;
    uint64_t maximumInsertionSequence = nativeInsertionSequence;
    if (std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}} <
        std::tuple{maximumRegion, maximumRank, maximumInsertionSequence}) {
      maximumRegion = barrierRegion;
      maximumRank = 0;
      maximumInsertionSequence = 0;
    }
    bool designProgress = false;
    obelisk_rt_status designStatus = obelisk_rt_run_one_design_task(
        context, maximumRegion, maximumRank, maximumInsertionSequence,
        &designProgress);
    if (designStatus != OBELISK_RT_OK)
      return designStatus;
    if (designProgress) {
      obelisk_rt_status status = recordSlotProgress();
      if (status != OBELISK_RT_OK)
        return status;
      if (context->nativeScheduleSingleStep)
        return OBELISK_RT_OK;
      continue;
    }
    obelisk_rt_process_instance_v1 *selected = nullptr;
    size_t selectedIndex = 0;
    {
      ContextMutexLock lock(context);
      const size_t processCount = context->scheduledProcesses.size();
      uint32_t selectedRank = UINT32_MAX;
      uint32_t selectedRegion = UINT32_MAX;
      uint64_t selectedInsertionSequence = UINT64_MAX;
      bool scanStateUnchanged =
          processCount == nativeScanProcessCount &&
          context->schedulerEpoch == nativeScanEpoch &&
          context->schedulerSelectionGeneration ==
              nativeScanSelectionGeneration &&
          context->nextProcessInsertionSequence == nativeScanInsertionSequence;
      bool nativeCandidateExpected =
          nativeCandidateIndex != SIZE_MAX &&
          (std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} ==
               std::tuple{uint32_t{0}, uint32_t{0}, uint64_t{0}} ||
           std::tuple{nativeRegion, nativeRank, nativeInsertionSequence} <
               std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}});
      if (scanStateUnchanged && nativeCandidateExpected &&
          nativeCandidateIndex < processCount) {
        ScheduledProcess &candidate =
            context->scheduledProcesses[nativeCandidateIndex];
        bool runnable =
            candidate.instance && candidate.token == nativeCandidateToken &&
            candidate.phase == (context->schedulerRunningFinals ? 1u : 0u) &&
            nativeProcessReady(*context, candidate,
                               context->nativeScheduleForcedSlot != UINT32_MAX);
        auto key = std::tuple{candidate.queuedRegion, candidate.scheduleRank,
                              candidate.insertionSequence};
        if (runnable &&
            (candidate.urgent ||
             (key == std::tuple{nativeRegion, nativeRank,
                                nativeInsertionSequence} &&
              key < std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}}))) {
          selected = candidate.instance;
          selectedIndex = nativeCandidateIndex;
          selectedRank = candidate.urgent ? 0 : candidate.scheduleRank;
          selectedRegion = candidate.urgent ? 0 : candidate.queuedRegion;
          selectedInsertionSequence =
              candidate.urgent ? 0 : candidate.insertionSequence;
        }
      }
      if (!selected && (!scanStateUnchanged || nativeCandidateExpected)) {
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.fallbackRescans;
        for (size_t step = 0; step < processCount; ++step) {
          size_t index = (context->schedulerCursor + step) % processCount;
          ScheduledProcess &candidate = context->scheduledProcesses[index];
          if (context->signalDiagnosticsEnabled)
            ++context->signalDiagnostics.candidateScans;
          if (!candidate.instance ||
              candidate.phase != (context->schedulerRunningFinals ? 1u : 0u))
            continue;
          bool runnable = nativeProcessReady(*context, candidate, false);
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
          auto key = std::tuple{candidate.queuedRegion, candidate.scheduleRank,
                                candidate.insertionSequence};
          if (!(key < std::tuple{barrierRegion, uint32_t{0}, uint64_t{0}}))
            continue;
          if (selected && !(key < std::tuple{selectedRegion, selectedRank,
                                             selectedInsertionSequence}))
            continue;
          selected = candidate.instance;
          selectedIndex = index;
          selectedRank = candidate.scheduleRank;
          selectedRegion = candidate.queuedRegion;
          selectedInsertionSequence = candidate.insertionSequence;
        }
      }
      if (selected) {
        context->schedulerCursor = (selectedIndex + 1) % processCount;
        ScheduledProcess &candidate =
            context->scheduledProcesses[selectedIndex];
        obelisk_rt_unregister_signal_wait_unlocked(
            context, candidate.signalSubscriptions, candidate.token, false);
        context->nativePollCandidates.erase(candidate.token);
        candidate.started = true;
        candidate.observedEpoch = context->schedulerEpoch;
      }
      if (!selected && !context->schedulerRunningFinals &&
          barrierRegion != UINT32_MAX) {
        bool changed = false;
        bool eventTriggered = false;
        auto applyNative = [&](const ScheduledNBA &update) {
          bool publicationChanged = false;
          uint32_t automaticID = 0;
          uint32_t staticID = 0;
          int64_t baseOffset = 0;
          bool automatic =
              decodeNativeAutomatic(update.bitOffset, automaticID, baseOffset);
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
            const NativeStaticState *state =
                findNativeStaticState(context, staticID);
            if (!state || state->bitOffset > update.planeBitCount ||
                state->bitWidth > update.planeBitCount - state->bitOffset) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            staticState = state;
          } else if (!decodeNativeGlobal(update.bitOffset, baseOffset)) {
            context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
            return;
          }
          bool canonical =
              !automatic && context->execution &&
              context->execution->state_bit_count == update.planeBitCount;
          if (canonical &&
              (context->stateValue.size() != (update.planeBitCount + 63) / 64 ||
               context->stateUnknown.size() != context->stateValue.size())) {
            context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
            return;
          }
          if (update.managedValue) {
            if (update.bitWidth != 64 || baseOffset < 0 ||
                (static_cast<uint64_t>(baseOffset) & 63) != 0 ||
                update.value.size() != sizeof(obelisk_rt_object_v1 *) ||
                update.unknown.size() != 0 ||
                (update.rootedManaged &&
                 obelisk_rt_managed_object_context(update.rootedManaged) !=
                     context)) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            obelisk_rt_object_v1 *previous = nullptr;
            uint64_t byteOffset = static_cast<uint64_t>(baseOffset) / 8;
            if (automatic) {
              if (automaticState->managedRootRegistered) {
                if (baseOffset != 0) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                previous = automaticState->managedValue;
                automaticState->managedValue = update.rootedManaged;
              } else {
                if (byteOffset > automaticState->value.size() ||
                    sizeof(previous) >
                        automaticState->value.size() - byteOffset ||
                    std::find(automaticState->managedRootByteOffsets.begin(),
                              automaticState->managedRootByteOffsets.end(),
                              byteOffset) ==
                        automaticState->managedRootByteOffsets.end()) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                std::memcpy(&previous,
                            automaticState->value.data() + byteOffset,
                            sizeof(previous));
                std::memcpy(automaticState->value.data() + byteOffset,
                            &update.rootedManaged,
                            sizeof(update.rootedManaged));
              }
            } else {
              if (!canonical) {
                context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
                return;
              }
              uint64_t planeBit = staticState
                                      ? staticState->bitOffset +
                                            static_cast<uint64_t>(baseOffset)
                                      : static_cast<uint64_t>(baseOffset);
              if ((planeBit & 63) != 0 ||
                  planeBit / 64 >= context->stateValue.size()) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              }
              std::memcpy(&previous, &context->stateValue[planeBit / 64],
                          sizeof(previous));
              std::memcpy(&context->stateValue[planeBit / 64],
                          &update.rootedManaged, sizeof(update.rootedManaged));
            }
            if (previous != update.rootedManaged) {
              changed = true;
              publicationChanged = true;
              if (!obelisk_rt_publish_signal_occurrence_unlocked(
                      context, update.bitOffset, 64, OBELISK_RT_SIGNAL_CHANGE))
                return;
              obelisk_rt_invalidate_signal_snapshots_unlocked(
                  context, update.bitOffset, 64);
              if (!obelisk_rt_latch_conditional_signal_range_unlocked(
                      context, update.bitOffset, 64, OBELISK_RT_SIGNAL_CHANGE))
                return;
              if (!obelisk_rt_notify_observer_signal_unlocked(
                      context, update.bitOffset, 64))
                return;
            }
            return;
          }
          bool equalStringContents = false;
          if (update.stringValue) {
            if (update.bitWidth != 64 || baseOffset < 0 ||
                (static_cast<uint64_t>(baseOffset) & 63) != 0 ||
                obelisk_rt_validate_string(context, update.rootedString) !=
                    OBELISK_RT_OK) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            obelisk_rt_string_v1 previous = 0;
            uint64_t byteOffset = static_cast<uint64_t>(baseOffset) / 8;
            if (automatic) {
              if (automaticState->managedRootRegistered) {
                if (baseOffset != 0) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                std::memcpy(&previous, &automaticState->managedValue,
                            sizeof(previous));
              } else if (byteOffset > automaticState->value.size() ||
                         sizeof(previous) >
                             automaticState->value.size() - byteOffset) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              } else {
                std::memcpy(&previous,
                            automaticState->value.data() + byteOffset,
                            sizeof(previous));
              }
            } else {
              uint64_t planeBit = staticState
                                      ? staticState->bitOffset +
                                            static_cast<uint64_t>(baseOffset)
                                      : static_cast<uint64_t>(baseOffset);
              if ((planeBit & 63) != 0) {
                context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                return;
              }
              if (canonical) {
                if (planeBit / 64 >= context->stateValue.size()) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                previous = context->stateValue[planeBit / 64];
              } else {
                uint64_t globalByte = planeBit / 8;
                uint64_t globalBytes = (update.planeBitCount + 7) / 8;
                if (!update.valuePlane || globalByte > globalBytes ||
                    sizeof(previous) > globalBytes - globalByte) {
                  context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
                  return;
                }
                std::memcpy(&previous, update.valuePlane + globalByte,
                            sizeof(previous));
              }
            }
            if (obelisk_rt_validate_string(context, previous) !=
                OBELISK_RT_OK) {
              context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
              return;
            }
            equalStringContents = obelisk_rt_v1_string_compare(
                                      previous, update.rootedString) == 0;
          }
          PackedSignalTransitionBuffer transitions(update.bitWidth);
          uint64_t packedPlaneBit = 0;
          bool smallCanonicalPacked =
              canonical && !automatic && !update.stringValue &&
              baseOffset >= 0 && update.bitWidth <= 64 &&
              (update.inlinePacked ||
               update.value.size() == (update.bitWidth + 7) / 8);
          if (smallCanonicalPacked) {
            if (staticState)
              smallCanonicalPacked =
                  static_cast<uint64_t>(baseOffset) <= staticState->bitWidth &&
                  update.bitWidth <=
                      staticState->bitWidth - static_cast<uint64_t>(baseOffset);
            packedPlaneBit = staticState ? staticState->bitOffset +
                                               static_cast<uint64_t>(baseOffset)
                                         : static_cast<uint64_t>(baseOffset);
            smallCanonicalPacked &=
                packedPlaneBit <= update.planeBitCount &&
                update.bitWidth <= update.planeBitCount - packedPlaneBit;
          }
          auto rangeMasked = [&](const std::vector<uint64_t> &masks) {
            if (masks.empty())
              return false;
            uint64_t first = packedPlaneBit / 64;
            uint64_t last = (packedPlaneBit + update.bitWidth - 1) / 64;
            for (uint64_t limb = first; limb <= last && limb < masks.size();
                 ++limb) {
              uint64_t begin =
                  limb == first ? packedPlaneBit % 64 : uint64_t{0};
              uint64_t end =
                  limb == last ? (packedPlaneBit + update.bitWidth - 1) % 64 + 1
                               : uint64_t{64};
              uint64_t mask =
                  begin == 0 && end == 64
                      ? UINT64_MAX
                      : (end == 64 ? UINT64_MAX : (uint64_t{1} << end) - 1) &
                            (begin == 0 ? UINT64_MAX
                                        : ~((uint64_t{1} << begin) - 1));
              if ((masks[limb] & mask) != 0)
                return true;
            }
            return false;
          };
          smallCanonicalPacked &= !rangeMasked(context->forceMask) &&
                                  !rangeMasked(context->assignMask);
          if (smallCanonicalPacked) {
            auto *canonicalValue =
                reinterpret_cast<uint8_t *>(context->stateValue.data());
            auto *canonicalUnknown =
                reinterpret_cast<uint8_t *>(context->stateUnknown.data());
            uint64_t oldValue = loadPackedBytes(canonicalValue, packedPlaneBit,
                                                update.bitWidth);
            uint64_t oldUnknown = loadPackedBytes(
                canonicalUnknown, packedPlaneBit, update.bitWidth);
            uint64_t newValue =
                update.inlinePacked
                    ? update.inlineValue
                    : loadPackedBytes(update.value.data(), 0, update.bitWidth);
            uint64_t newUnknown = update.inlinePacked ? update.inlineUnknown
                                  : update.unknown.empty()
                                      ? 0
                                      : loadPackedBytes(update.unknown.data(),
                                                        0, update.bitWidth);
            uint64_t widthMask = update.bitWidth == 64
                                     ? UINT64_MAX
                                     : (uint64_t{1} << update.bitWidth) - 1;
            uint64_t changedBits =
                ((oldValue ^ newValue) | (oldUnknown ^ newUnknown)) & widthMask;
            uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
            uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
            uint64_t newZero = ~newUnknown & ~newValue & widthMask;
            uint64_t newOne = ~newUnknown & newValue & widthMask;
            uint64_t posedge = (oldZero & ~newZero) | (oldUnknown & newOne);
            uint64_t negedge = (oldOne & ~newOne) | (oldUnknown & newZero);
            uint64_t packedBytes = (update.bitWidth + 7) / 8;
            std::memcpy(transitions.changed(), &changedBits, packedBytes);
            std::memcpy(transitions.posedge(), &posedge, packedBytes);
            std::memcpy(transitions.negedge(), &negedge, packedBytes);
            if (update.valuePlane)
              storePackedBytes(update.valuePlane, packedPlaneBit,
                               update.bitWidth, newValue);
            if (update.unknownPlane)
              storePackedBytes(update.unknownPlane, packedPlaneBit,
                               update.bitWidth, newUnknown);
            storePackedBytes(canonicalValue, packedPlaneBit, update.bitWidth,
                             newValue);
            storePackedBytes(canonicalUnknown, packedPlaneBit, update.bitWidth,
                             newUnknown);
            publicationChanged = changedBits != 0;
            changed |= publicationChanged;
          } else
            for (uint64_t bit = 0; bit < update.bitWidth; ++bit) {
              uint64_t sourceByte = bit / 8;
              uint8_t sourceMask = static_cast<uint8_t>(1u << (bit % 8));
              if (!update.inlinePacked &&
                  (sourceByte >= update.value.size() ||
                   (!update.unknown.empty() &&
                    sourceByte >= update.unknown.size()))) {
                context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
                return;
              }
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
              uint64_t planeBit =
                  staticState ? staticState->bitOffset + localBit : localBit;
              uint64_t canonicalMask = uint64_t{1} << (planeBit % 64);
              bool forced =
                  planeBit / 64 < context->forceMask.size() &&
                  (context->forceMask[planeBit / 64] & canonicalMask) != 0;
              bool assigned =
                  planeBit / 64 < context->assignMask.size() &&
                  (context->assignMask[planeBit / 64] & canonicalMask) != 0;
              if (canonical && (forced || assigned))
                continue;
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
              bool newValue =
                  update.inlinePacked
                      ? ((update.inlineValue >> bit) & uint64_t{1}) != 0
                      : (update.value[sourceByte] & sourceMask) != 0;
              bool newUnknown =
                  update.inlinePacked
                      ? ((update.inlineUnknown >> bit) & uint64_t{1}) != 0
                      : !update.unknown.empty() &&
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
              uint32_t edges =
                  transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
              if (edges != 0 && !equalStringContents) {
                changed = true;
                publicationChanged = true;
                transitions.record(bit, edges);
              }
            }
          if (publicationChanged) {
            uint64_t sequence = 0;
            if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                    context, update.bitOffset, update.bitWidth,
                    transitions.changed(), transitions.posedge(),
                    transitions.negedge(), 0, &sequence))
              return;
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, update.bitOffset, update.bitWidth);
            if (obelisk_rt_has_conditional_signal_waiters(context)) {
              for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
                if (!byteBit(transitions.changed(), bit) ||
                    bit > static_cast<uint64_t>(INT64_MAX))
                  continue;
                uint64_t eventHandle = nativeHandleOffset(
                    update.bitOffset, static_cast<int64_t>(bit));
                if (eventHandle == UINT64_MAX)
                  continue;
                bool finalValue =
                    update.inlinePacked
                        ? ((update.inlineValue >> bit) & uint64_t{1}) != 0
                        : byteBit(update.value.data(), bit);
                bool finalUnknown =
                    update.inlinePacked
                        ? ((update.inlineUnknown >> bit) & uint64_t{1}) != 0
                        : !update.unknown.empty() &&
                              byteBit(update.unknown.data(), bit);
                context->signalValueSnapshots[eventHandle] = {
                    sequence, finalValue, finalUnknown};
              }
              for (uint64_t bit = 0; bit != update.bitWidth; ++bit) {
                if (!byteBit(transitions.changed(), bit) ||
                    bit > static_cast<uint64_t>(INT64_MAX))
                  continue;
                uint64_t eventHandle = nativeHandleOffset(
                    update.bitOffset, static_cast<int64_t>(bit));
                if (eventHandle == UINT64_MAX)
                  continue;
                uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
                if (byteBit(transitions.posedge(), bit))
                  edges |= OBELISK_RT_SIGNAL_POSEDGE;
                if (byteBit(transitions.negedge(), bit))
                  edges |= OBELISK_RT_SIGNAL_NEGEDGE;
                if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                        context, eventHandle, edges))
                  return;
              }
            }
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, update.bitOffset, update.bitWidth))
              return;
          }
        };
        auto planeBit = [](const std::vector<uint64_t> &plane, uint64_t bit) {
          return bit / 64 < plane.size() &&
                 ((plane[bit / 64] >> (bit % 64)) & 1) != 0;
        };
        auto applyDesign = [&](const ScheduledDesignNBA &update) {
          if (update.handleKind != OBELISK_RT_DESCRIPTOR_STORAGE)
            return false;
          bool publicationChanged = false;
          uint64_t publicationBegin = UINT64_MAX;
          uint64_t publicationEnd = 0;
          uint64_t available =
              context->execution ? context->execution->state_bit_count : 0;
          PackedSignalTransitionBuffer transitions(update.bitWidth);
          bool equalStringContents = false;
          if (update.stringValue) {
            if (update.bitWidth != 64 || update.start < update.begin ||
                update.start < 0 || (update.start & 63) != 0 ||
                update.end < update.start || update.end - update.start < 64 ||
                static_cast<uint64_t>(update.start) > available ||
                64 > available - static_cast<uint64_t>(update.start) ||
                obelisk_rt_validate_string(context, update.rootedString) !=
                    OBELISK_RT_OK)
              return false;
            obelisk_rt_string_v1 previous =
                context->stateValue[static_cast<uint64_t>(update.start) / 64];
            if (obelisk_rt_validate_string(context, previous) != OBELISK_RT_OK)
              return false;
            equalStringContents = obelisk_rt_v1_string_compare(
                                      previous, update.rootedString) == 0;
          }
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
            bool forced = destination / 64 < context->forceMask.size() &&
                          (context->forceMask[destination / 64] & mask) != 0;
            bool assigned = destination / 64 < context->assignMask.size() &&
                            (context->assignMask[destination / 64] & mask) != 0;
            if (forced || assigned)
              continue;
            bool oldValue = (context->stateValue[limb] & mask) != 0;
            bool oldUnknown = (context->stateUnknown[limb] & mask) != 0;
            bool newValue = planeBit(update.value, bit);
            bool newUnknown = planeBit(update.unknown, bit);
            publicationChanged |=
                !equalStringContents &&
                (oldValue != newValue || oldUnknown != newUnknown);
            publicationBegin = std::min(publicationBegin, destination);
            publicationEnd = std::max(publicationEnd, destination + 1);
            auto apply = [&](std::vector<uint64_t> &plane, bool value) {
              uint64_t old = plane[limb];
              uint64_t next = value ? old | mask : old & ~mask;
              changed |= !equalStringContents && old != next;
              plane[limb] = next;
            };
            apply(context->stateValue, newValue);
            apply(context->stateUnknown, newUnknown);
            uint32_t edges =
                transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
            if (edges != 0 && !equalStringContents) {
              transitions.record(bit, edges);
            }
          }
          if (publicationChanged) {
            uint64_t publicationWidth = publicationEnd - publicationBegin;
            uint64_t edgeBitOffset = static_cast<uint64_t>(
                static_cast<__int128>(publicationBegin) - update.start);
            uint64_t sequence = 0;
            if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                    context, publicationBegin, publicationWidth,
                    transitions.changed(), transitions.posedge(),
                    transitions.negedge(), edgeBitOffset, &sequence))
              return false;
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, publicationBegin, publicationWidth);
            if (obelisk_rt_has_conditional_signal_waiters(context)) {
              for (uint64_t bit = 0; bit != publicationWidth; ++bit) {
                uint64_t sourceBit = edgeBitOffset + bit;
                if (!byteBit(transitions.changed(), sourceBit))
                  continue;
                uint64_t destination = publicationBegin + bit;
                uint64_t mask = uint64_t{1} << (destination % 64);
                context->signalValueSnapshots[destination] = {
                    sequence,
                    (context->stateValue[destination / 64] & mask) != 0,
                    (context->stateUnknown[destination / 64] & mask) != 0};
              }
              for (uint64_t bit = 0; bit != publicationWidth; ++bit) {
                uint64_t sourceBit = edgeBitOffset + bit;
                if (!byteBit(transitions.changed(), sourceBit))
                  continue;
                uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
                if (byteBit(transitions.posedge(), sourceBit))
                  edges |= OBELISK_RT_SIGNAL_POSEDGE;
                if (byteBit(transitions.negedge(), sourceBit))
                  edges |= OBELISK_RT_SIGNAL_NEGEDGE;
                if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                        context, publicationBegin + bit, edges))
                  return false;
              }
            }
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, publicationBegin, publicationWidth))
              return false;
          }
          return true;
        };
        // Native NBA entries are appended in global execution-sequence order.
        // Purely digital native designs therefore need neither the generic
        // four-queue merge nor one vector erase per committed assignment.
        // Compact future/other-region entries once after draining this
        // barrier; this changes the common case from quadratic to linear work.
        obelisk_rt_status staticNBAStatus = commitStaticNBAAccumulatorsUnlocked(
            context, barrierRegion, changed);
        if (staticNBAStatus != OBELISK_RT_OK)
          return staticNBAStatus;
        if (context->scheduledManagedNBAs.empty() &&
            context->scheduledDesignNBAs.empty() &&
            context->scheduledDesignEvents.empty()) {
          size_t retained = 0;
          for (size_t index = 0; index != context->scheduledNBAs.size();
               ++index) {
            ScheduledNBA &update = context->scheduledNBAs[index];
            bool due = update.dueTime <= context->schedulerTime &&
                       update.execRegion == barrierRegion;
            if (!due) {
              if (retained != index)
                context->scheduledNBAs[retained] = std::move(update);
              ++retained;
              continue;
            }
            uint32_t retainedAutomaticID = update.retainedAutomaticID;
            applyNative(update);
            if (context->schedulerStatus != OBELISK_RT_OK)
              return context->schedulerStatus;
            if (retainedAutomaticID != 0) {
              auto found =
                  context->nativeAutomaticStates.find(retainedAutomaticID);
              if (found == context->nativeAutomaticStates.end() ||
                  found->second.referenceCount == 0)
                return OBELISK_RT_INVALID_HANDLE;
              if (--found->second.referenceCount == 0) {
                obelisk_rt_erase_automatic_bookkeeping_unlocked(
                    context, retainedAutomaticID);
                context->nativeAutomaticStates.erase(found);
              }
            }
          }
          context->scheduledNBAs.resize(retained);
        } else
          for (;;) {
            uint64_t nativeSequence = UINT64_MAX;
            size_t nativeIndex = 0;
            for (size_t index = 0; index != context->scheduledNBAs.size();
                 ++index) {
              const ScheduledNBA &update = context->scheduledNBAs[index];
              if (update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
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
                  event.execRegion == barrierRegion &&
                  event.sequence < eventSequence) {
                eventSequence = event.sequence;
                eventIndex = index;
              }
            }
            uint64_t designSequence = UINT64_MAX;
            size_t designIndex = 0;
            for (size_t index = 0; index != context->scheduledDesignNBAs.size();
                 ++index) {
              const ScheduledDesignNBA &update =
                  context->scheduledDesignNBAs[index];
              if (update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
                  update.sequence < designSequence) {
                designSequence = update.sequence;
                designIndex = index;
              }
            }
            uint64_t managedSequence = UINT64_MAX;
            size_t managedIndex = 0;
            for (size_t index = 0;
                 index != context->scheduledManagedNBAs.size(); ++index) {
              const ScheduledManagedNBA &update =
                  context->scheduledManagedNBAs[index];
              if (update.dueTime <= context->schedulerTime &&
                  update.execRegion == barrierRegion &&
                  update.sequence < managedSequence) {
                managedSequence = update.sequence;
                managedIndex = index;
              }
            }
            uint64_t sequence =
                std::min(std::min(nativeSequence, managedSequence),
                         std::min(eventSequence, designSequence));
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
                  obelisk_rt_erase_automatic_bookkeeping_unlocked(
                      context, retainedAutomaticID);
                  context->nativeAutomaticStates.erase(found);
                }
              }
              context->scheduledNBAs.erase(context->scheduledNBAs.begin() +
                                           nativeIndex);
            } else if (sequence == managedSequence) {
              obelisk_rt_status status = obelisk_rt_apply_managed_nba(
                  context, context->scheduledManagedNBAs[managedIndex]);
              context->scheduledManagedNBAs.erase(
                  context->scheduledManagedNBAs.begin() + managedIndex);
              if (status != OBELISK_RT_OK)
                return status;
            } else if (sequence == eventSequence) {
              uint64_t stableID =
                  context->scheduledDesignEvents[eventIndex].stableID;
              uint32_t retainedAutomaticID =
                  context->scheduledDesignEvents[eventIndex]
                      .retainedAutomaticID;
              context->scheduledDesignEvents.erase(
                  context->scheduledDesignEvents.begin() + eventIndex);
              EventState &event = context->events[stableID];
              if (++event.generation == 0)
                event.generation = 1;
              event.lastTriggeredTime = context->schedulerTime;
              bool notified =
                  obelisk_rt_notify_observer_event_unlocked(context, stableID);
              if (retainedAutomaticID != 0) {
                auto found =
                    context->nativeAutomaticStates.find(retainedAutomaticID);
                if (found == context->nativeAutomaticStates.end() ||
                    found->second.referenceCount == 0)
                  return OBELISK_RT_INVALID_HANDLE;
                if (--found->second.referenceCount == 0) {
                  obelisk_rt_erase_automatic_bookkeeping_unlocked(
                      context, retainedAutomaticID);
                  context->nativeAutomaticStates.erase(found);
                }
              }
              if (!notified)
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
        if (context->schedulerSlotProgress == maxSlotProgress) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
          return context->schedulerStatus;
        }
        ++context->schedulerSlotProgress;
        if (context->nativeScheduleSingleStep)
          return OBELISK_RT_OK;
        continue;
      }
      if (!selected && !context->schedulerRunningFinals) {
        std::fill(context->staticNBASlowRoots.begin(),
                  context->staticNBASlowRoots.end(), uint8_t{0});
        // Writable VPI deposits temporarily select bytecode for affected
        // bytecode-capable static actors. Re-enter native execution only after
        // the current slot is quiescent, never between fragments or regions.
        if (context->nativeScheduleExternalWritePending) {
          if (context->nativeSchedulePlan->state_bit_count != 0 &&
              !reconcileNativeDirtyRootsToPlanesUnlocked(
                  context, context->nativeSchedulePlan)) {
            context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
            return context->schedulerStatus;
          }
          for (obelisk_rt_process_instance_v1 *actor :
               context->nativeScheduleActors)
            if (actor && actor->lifecycle != OBELISK_RT_PROCESS_TERMINATED &&
                (actor->descriptor->available_tiers &
                 OBELISK_RT_TIER_MASK_NATIVE) != 0)
              actor->tier = OBELISK_RT_TIER_NATIVE;
          context->nativeScheduleExternalWritePending = false;
          context->nativeScheduleTransientDirtyRoots.clear();
          std::fill(context->nativeScheduleTransientDirtyMask.begin(),
                    context->nativeScheduleTransientDirtyMask.end(),
                    uint8_t{0});
          context->nativeScheduleDirtyRootsPresent =
              !context->nativeSchedulePersistentDirtyRoots.empty();
        }
        refreshNativeStaticSpecializationFastUnlocked(context);
        std::optional<uint64_t> nextTime;
        auto considerTime = [&](uint64_t candidate) {
          if (candidate > context->schedulerTime &&
              (!nextTime || candidate < *nextTime))
            nextTime = candidate;
        };
        for (const ScheduledProcess &candidate : context->scheduledProcesses)
          if (candidate.instance && candidate.phase == 0 && candidate.started &&
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
        for (const ScheduledManagedNBA &update : context->scheduledManagedNBAs)
          if (update.dueTime > context->schedulerTime)
            considerTime(update.dueTime);
        for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
          if (event.dueTime > context->schedulerTime)
            considerTime(event.dueTime);
        if (nextTime) {
          context->schedulerTime = *nextTime;
          if (context->nativeScheduleSingleStep)
            return OBELISK_RT_OK;
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
          if (context->nativeScheduleSingleStep)
            return OBELISK_RT_OK;
          continue;
        }
      }
    }
    if (!selected) {
      ContextMutexLock lock(context);
      return context->schedulerFinishStatus;
    }

    if (context->nativeScheduleForcedSlot != UINT32_MAX)
      context->nativeScheduleForcedExecuted = true;
    {
      ContextMutexLock lock(context);
      if (context->activeNativeProcess)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->activeNativeProcess = selected;
      context->activeHomeRegion =
          context->scheduledProcesses[selectedIndex].homeRegion;
      context->activeExecRegion =
          context->scheduledProcesses[selectedIndex].queuedRegion;
      context->activeLogicalProcessToken =
          kNativeLogicalProcessTag |
          context->scheduledProcesses[selectedIndex].token;
      context->activeControls =
          std::move(context->scheduledProcesses[selectedIndex].controls);
    }
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_execution_tier tier = selected->tier;
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[selectedIndex];
    bool requireBytecode =
        context->execution && (context->execution->flags &
                               OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
    if (requireBytecode) {
      tier = OBELISK_RT_TIER_BYTECODE;
    } else if (scheduled.aotActorSlot != UINT32_MAX) {
      bool bytecodeFragment = std::binary_search(
          scheduled.bytecodeContinuations.begin(),
          scheduled.bytecodeContinuations.end(), selected->continuation);
      tier =
          bytecodeFragment ? OBELISK_RT_TIER_BYTECODE : OBELISK_RT_TIER_NATIVE;
      // A writable VPI transition keeps all affected static actors on
      // bytecode until the event slot reaches a quiescent boundary.
      if (context->nativeScheduleExternalWritePending &&
          (scheduled.aotActorSlot == UINT32_MAX ||
           nativeAOTActorDirty(context, scheduled.aotActorSlot)) &&
          (selected->descriptor->available_tiers &
           OBELISK_RT_TIER_MASK_BYTECODE) != 0)
        tier = OBELISK_RT_TIER_BYTECODE;
    }
    if (tier != OBELISK_RT_TIER_NATIVE && tier != OBELISK_RT_TIER_BYTECODE)
      tier =
          (selected->descriptor->available_tiers & OBELISK_RT_TIER_MASK_NATIVE)
              ? OBELISK_RT_TIER_NATIVE
              : OBELISK_RT_TIER_BYTECODE;
    obelisk_rt_status status = obelisk_rt_v1_process_instance_execute(
        selected, context, tier, &action);
    bool terminationRequested = false;
    {
      ContextMutexLock lock(context);
      if (selectedIndex < context->scheduledProcesses.size() &&
          context->scheduledProcesses[selectedIndex].instance == selected)
        context->scheduledProcesses[selectedIndex].controls =
            std::move(context->activeControls);
      context->activeControls.clear();
      context->activeNativeProcess = nullptr;
      context->activeHomeRegion = UINT32_MAX;
      context->activeExecRegion = UINT32_MAX;
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
          (callee->ownership_context && callee->ownership_context != context))
        return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (terminationRequested)
      action = {
          OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    bool destroy = false;
    std::vector<obelisk_rt_process_instance_v1 *> terminatedCallers;
    {
      ContextMutexLock lock(context);
      if (selectedIndex >= context->scheduledProcesses.size() ||
          context->scheduledProcesses[selectedIndex].instance != selected)
        return OBELISK_RT_INVALID_LIFECYCLE;
      ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
      if (action.kind == OBELISK_RT_FRAGMENT_TERMINATE) {
        if (!scheduled.callers.empty() && !context->schedulerFinishRequested) {
          scheduled.instance = scheduled.callers.back();
          scheduled.callers.pop_back();
          scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
          scheduled.waitOffset = 0;
          scheduled.waitSize = 0;
          scheduled.waitGenerations.clear();
          scheduled.signalTriggered = false;
          scheduled.urgent = true;
          scheduled.queuedRegion = scheduled.homeRegion;
          destroy = true;
        } else {
          uint64_t token = scheduled.token;
          context->terminatedNativeProcesses.insert(token);
          scheduled.instance = nullptr;
          ++context->schedulerDeadProcessCount;
          context->schedulerCompactionPending = true;
          if (terminationRequested)
            terminatedCallers.swap(scheduled.callers);
          obelisk_rt_release_controls_unlocked(context, scheduled.controls);
          scheduled.controls.clear();
          destroy = true;
          scheduled.signalTriggered = false;
          scheduled.urgent = false;
          if (++context->schedulerEpoch == 0)
            context->schedulerEpoch = 1;
        }
      } else if (action.kind == OBELISK_RT_FRAGMENT_SUSPEND) {
        status = adoptScheduledSuspendUnlocked(context, scheduled, action);
        if (status != OBELISK_RT_OK)
          return status;
      } else if (action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
        auto *callee = pendingCallee.get();
        if (!callee)
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (scheduled.callers.size() == std::numeric_limits<size_t>::max())
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
        scheduled.queuedRegion = scheduled.homeRegion;
        pendingCallee.release();
      } else {
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = false;
        scheduled.queuedRegion = scheduled.homeRegion;
      }
      if (destroy && scheduled.aotActorSlot != UINT32_MAX) {
        uint32_t slot = scheduled.aotActorSlot;
        if (!context->nativeSchedulePlan ||
            slot >= context->nativeScheduleActors.size())
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (context->nativeScheduleActors[slot] != scheduled.instance) {
          if (context->nativeScheduleActors[slot] != selected)
            return OBELISK_RT_INVALID_LIFECYCLE;
          status = context->nativeSchedulePlan->bind(
              context->nativeSchedulePlan->mutable_state, context, slot,
              scheduled.instance);
          if (status != OBELISK_RT_OK)
            return status;
          context->nativeScheduleActors[slot] = scheduled.instance;
          context->nativeScheduleActorTokens[slot] =
              scheduled.instance ? scheduled.token : 0;
          context->nativeScheduleActorIndices[slot] =
              scheduled.instance ? selectedIndex : SIZE_MAX;
        }
      }
      if (scheduled.instance && !indexedSignalBlocked(scheduled))
        context->nativePollCandidates.insert(scheduled.token);
      else
        context->nativePollCandidates.erase(scheduled.token);
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
    status = recordSlotProgress();
    if (status != OBELISK_RT_OK)
      return status;
    if (context->nativeScheduleSingleStep)
      return OBELISK_RT_OK;
  }
}

namespace {

obelisk_rt_status executeStaticNativeAOT(
    obelisk_rt_process_instance_v1 *instance, obelisk_rt_context *context,
    obelisk_rt_fragment_action_v1 &action, bool generatedActions) {
  action = {OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (!instance || !context || !instance->descriptor ||
      !instance->descriptor->native_execute ||
      (instance->descriptor->available_tiers & OBELISK_RT_TIER_MASK_NATIVE) ==
          0 ||
      instance->descriptor->execution != context->execution ||
      instance->lifecycle == OBELISK_RT_PROCESS_EXECUTING ||
      instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED ||
      (!generatedActions &&
       !validContinuation(*instance->descriptor->frame_layout,
                          instance->continuation)) ||
      (instance->ownership_context && instance->ownership_context != context))
    return OBELISK_RT_INVALID_LIFECYCLE;
  instance->ownership_context = context;
  instance->tier = OBELISK_RT_TIER_NATIVE;
  instance->context = context;
  instance->action = &action;
  instance->status = OBELISK_RT_OK;
  instance->lifecycle = OBELISK_RT_PROCESS_EXECUTING;
  obelisk_rt_status status;
  try {
    status = instance->descriptor->native_execute(instance);
  } catch (const std::bad_alloc &) {
    status = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    status = OBELISK_RT_INVALID_ARGUMENT;
  }
  instance->context = nullptr;
  instance->action = nullptr;
  if (status == OBELISK_RT_OK && !generatedActions)
    status = validateAction(*instance, action, false);
  instance->status = status;
  if (status != OBELISK_RT_OK) {
    if (instance->native_handle) {
      instance->descriptor->native_destroy(instance);
      if (instance->native_handle)
        instance->status = status = OBELISK_RT_INVALID_LIFECYCLE;
    }
    instance->lifecycle = OBELISK_RT_PROCESS_SUSPENDED;
    return status;
  }
  instance->continuation = action.continuation;
  instance->lifecycle = action.kind == OBELISK_RT_FRAGMENT_TERMINATE
                            ? OBELISK_RT_PROCESS_TERMINATED
                        : action.kind == OBELISK_RT_FRAGMENT_SUSPEND
                            ? OBELISK_RT_PROCESS_SUSPENDED
                            : OBELISK_RT_PROCESS_READY;
  if (instance->lifecycle == OBELISK_RT_PROCESS_TERMINATED) {
    unregisterManagedFrameRoots(instance);
    releaseOwnedNativeStates(context, instance);
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status executeAOTNode(obelisk_rt_context *context,
                                 uint32_t actorSlot) {
  obelisk_rt_process_instance_v1 *selected = nullptr;
  size_t selectedIndex = SIZE_MAX;
  {
    ContextMutexLock lock(context);
    if (!context->nativeSchedulePlan ||
        actorSlot >= context->nativeScheduleActors.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    selected = context->nativeScheduleActors[actorSlot];
    uint64_t token = context->nativeScheduleActorTokens[actorSlot];
    selectedIndex = context->nativeScheduleActorIndices[actorSlot];
    if (!selected || token == 0 ||
        selectedIndex >= context->scheduledProcesses.size())
      return OBELISK_RT_INVALID_LIFECYCLE;
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    if (scheduled.instance != selected || scheduled.token != token ||
        scheduled.aotActorSlot != actorSlot)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->activeNativeProcess)
      return OBELISK_RT_INVALID_LIFECYCLE;
    scheduled.started = true;
    scheduled.observedEpoch = context->schedulerEpoch;
    context->activeNativeProcess = selected;
    context->activeHomeRegion = scheduled.homeRegion;
    context->activeExecRegion = scheduled.queuedRegion;
    context->activeLogicalProcessToken =
        kNativeLogicalProcessTag | scheduled.token;
    context->activeControls = std::move(scheduled.controls);
  }

  obelisk_rt_execution_tier tier = OBELISK_RT_TIER_NATIVE;
  {
    ContextMutexLock lock(context);
    const ScheduledProcess &scheduled =
        context->scheduledProcesses[selectedIndex];
    bool requireBytecode =
        context->execution && (context->execution->flags &
                               OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
    bool bytecodeFragment = std::binary_search(
        scheduled.bytecodeContinuations.begin(),
        scheduled.bytecodeContinuations.end(), selected->continuation);
    bool nativeRootBootstrap = actorSlot == 0 && selected->continuation == 0 &&
                               (context->nativeSchedulePlan->flags &
                                OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO) != 0;
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    bool guardedSpecialization = plan->specialization_fast != nullptr;
    bool specializationFast =
        !guardedSpecialization || *plan->specialization_fast != 0;
    // The native fused body is the clean version. Root-local VPI dirtiness
    // hands only intersecting actors to bytecode; a global reason such as an
    // observer, conditional waiter, or generic NBA hands over every actor for
    // the remainder of the affected slot.
    bool specializationHandover =
        guardedSpecialization && !specializationFast &&
        nativeAOTNeedsSpecializationHandoverUnlocked(context, actorSlot);
    bool needsBytecode =
        !nativeRootBootstrap &&
        (requireBytecode || bytecodeFragment || specializationHandover);
    if (needsBytecode) {
      if ((selected->descriptor->available_tiers &
           OBELISK_RT_TIER_MASK_BYTECODE) == 0)
        return OBELISK_RT_TIER_UNAVAILABLE;
      tier = OBELISK_RT_TIER_BYTECODE;
      if (specializationHandover && context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.aotStateSlowPaths;
    }
  }

  obelisk_rt_fragment_action_v1 action{};
  obelisk_rt_status status = OBELISK_RT_OK;
  bool generatedActions = tier == OBELISK_RT_TIER_NATIVE &&
                          context->nativeSchedulePlan &&
                          (context->nativeSchedulePlan->flags &
                           OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS) != 0;
  if (tier == OBELISK_RT_TIER_BYTECODE) {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    bool synchronized = plan && plan->state_bit_count == 0;
    if (plan && plan->state_bit_count != 0) {
      synchronized =
          (!context->nativeScheduleExternalWritePending ||
           reconcileNativeDirtyRootsToPlanesUnlocked(context, plan)) &&
          importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count);
    }
    if (!synchronized)
      status = OBELISK_RT_LAYOUT_MISMATCH;
  }
  if (status == OBELISK_RT_OK) {
    status = tier == OBELISK_RT_TIER_NATIVE
                 ? executeStaticNativeAOT(selected, context, action,
                                          generatedActions)
                 : obelisk_rt_v1_process_instance_execute(selected, context,
                                                          tier, &action);
  }
  if (tier == OBELISK_RT_TIER_BYTECODE) {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if ((!plan || (plan->state_bit_count != 0 &&
                   !exportNativeStatePlanesUnlocked(context, plan->state_value,
                                                    plan->state_unknown,
                                                    plan->state_bit_count))) &&
        status == OBELISK_RT_OK)
      status = OBELISK_RT_LAYOUT_MISMATCH;
  }
  bool terminationRequested = false;
  {
    ContextMutexLock lock(context);
    if (selectedIndex < context->scheduledProcesses.size() &&
        context->scheduledProcesses[selectedIndex].instance == selected)
      context->scheduledProcesses[selectedIndex].controls =
          std::move(context->activeControls);
    context->activeControls.clear();
    context->activeNativeProcess = nullptr;
    context->activeHomeRegion = UINT32_MAX;
    context->activeExecRegion = UINT32_MAX;
    context->activeLogicalProcessToken = 0;
    terminationRequested = context->schedulerFinishRequested;
    if (terminationRequested)
      context->schedulerRunningFinals = true;
  }
  if (!terminationRequested && status != OBELISK_RT_OK)
    return status;
  if (terminationRequested)
    action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};

  if (!terminationRequested && status == OBELISK_RT_OK && generatedActions &&
      action.kind == OBELISK_RT_FRAGMENT_SUSPEND &&
      (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ||
       action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
       action.suspend_kind == OBELISK_RT_SUSPEND_EDGE) &&
      (action.suspend_kind != OBELISK_RT_SUSPEND_DELAY ||
       reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
           static_cast<const uint8_t *>(selected->frame) + action.payload)
               ->payload != 0)) {
    ContextMutexLock lock(context);
    if (selectedIndex >= context->scheduledProcesses.size() ||
        context->scheduledProcesses[selectedIndex].instance != selected)
      return OBELISK_RT_INVALID_LIFECYCLE;
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    if (canUseStaticAOTFanout(context) &&
        !context->nativeScheduleExternalWritePending &&
        scheduled.signalSubscriptions.empty() &&
        scheduled.waitGenerations.empty()) {
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
      scheduled.signalTriggered = false;
      scheduled.urgent = false;
      const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          static_cast<const uint8_t *>(selected->frame) + action.payload);
      uint64_t delay =
          action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
      if (!obelisk_rt_next_queued_region(scheduled.homeRegion,
                                         action.suspend_kind, delay,
                                         action.flags, scheduled.queuedRegion))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
        scheduled.wakeTime = delay > UINT64_MAX - context->schedulerTime
                                 ? UINT64_MAX
                                 : context->schedulerTime + delay;
        setNativeAOTDeadlineUnlocked(context, actorSlot, scheduled.wakeTime);
      } else {
        removeNativeAOTDeadlineUnlocked(context, actorSlot);
        scheduled.signalLatch.reset();
      }
      if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return context->schedulerStatus;
      }
      ++context->schedulerSlotProgress;
      ++context->signalDiagnostics.aotNodeExecutions;
      return OBELISK_RT_OK;
    }
  }

  auto destroyPendingCallee =
      [](obelisk_rt_process_instance_v1 *instance) noexcept {
        if (instance)
          (void)obelisk_rt_v1_process_instance_destroy(instance);
      };
  std::unique_ptr<obelisk_rt_process_instance_v1,
                  decltype(destroyPendingCallee)>
      pendingCallee(nullptr, destroyPendingCallee);
  if (action.kind == OBELISK_RT_FRAGMENT_TASK_CALL) {
    auto *callee = reinterpret_cast<obelisk_rt_process_instance_v1 *>(
        static_cast<uintptr_t>(action.payload));
    if (!callee || callee == selected)
      return OBELISK_RT_INVALID_LIFECYCLE;
    pendingCallee.reset(callee);
    if (callee->lifecycle != OBELISK_RT_PROCESS_READY || !callee->descriptor ||
        callee->descriptor->execution != selected->descriptor->execution ||
        (callee->ownership_context && callee->ownership_context != context))
      return OBELISK_RT_INVALID_LIFECYCLE;
  }

  bool destroy = false;
  bool requestFallback = false;
  std::vector<obelisk_rt_process_instance_v1 *> terminatedCallers;
  {
    ContextMutexLock lock(context);
    if (selectedIndex >= context->scheduledProcesses.size() ||
        context->scheduledProcesses[selectedIndex].instance != selected)
      return OBELISK_RT_INVALID_LIFECYCLE;
    ScheduledProcess &scheduled = context->scheduledProcesses[selectedIndex];
    switch (action.kind) {
    case OBELISK_RT_FRAGMENT_TERMINATE:
      if (!scheduled.callers.empty() && !terminationRequested) {
        obelisk_rt_process_instance_v1 *caller = scheduled.callers.back();
        status = context->nativeSchedulePlan->bind(
            context->nativeSchedulePlan->mutable_state, context, actorSlot,
            caller);
        if (status != OBELISK_RT_OK)
          return status;
        scheduled.callers.pop_back();
        scheduled.instance = caller;
        scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
        scheduled.waitOffset = 0;
        scheduled.waitSize = 0;
        scheduled.waitGenerations.clear();
        scheduled.signalTriggered = false;
        scheduled.urgent = true;
        scheduled.queuedRegion = scheduled.homeRegion;
        context->nativeScheduleActors[actorSlot] = caller;
        context->nativeScheduleActorTokens[actorSlot] = scheduled.token;
        context->nativeScheduleActorIndices[actorSlot] = selectedIndex;
        destroy = true;
        requestFallback = true;
        break;
      }
      status = context->nativeSchedulePlan->bind(
          context->nativeSchedulePlan->mutable_state, context, actorSlot,
          nullptr);
      if (status != OBELISK_RT_OK)
        return status;
      context->terminatedNativeProcesses.insert(scheduled.token);
      obelisk_rt_unregister_signal_wait_unlocked(
          context, scheduled.signalSubscriptions, scheduled.token, false);
      scheduled.instance = nullptr;
      ++context->schedulerDeadProcessCount;
      context->schedulerCompactionPending = true;
      if (terminationRequested)
        terminatedCallers.swap(scheduled.callers);
      obelisk_rt_release_controls_unlocked(context, scheduled.controls);
      scheduled.controls.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = false;
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      destroy = true;
      break;
    case OBELISK_RT_FRAGMENT_SUSPEND: {
      if (action.suspend_kind != OBELISK_RT_SUSPEND_DELAY &&
          action.suspend_kind != OBELISK_RT_SUSPEND_CHANGE &&
          action.suspend_kind != OBELISK_RT_SUSPEND_EDGE) {
        status = adoptScheduledSuspendUnlocked(context, scheduled, action);
        if (status != OBELISK_RT_OK)
          return status;
        requestFallback = true;
        break;
      }
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
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = false;
      const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
          static_cast<const uint8_t *>(selected->frame) + action.payload);
      uint64_t delayPayload =
          action.suspend_kind == OBELISK_RT_SUSPEND_DELAY ? wait->payload : 1;
      if (!obelisk_rt_next_queued_region(scheduled.homeRegion,
                                         action.suspend_kind, delayPayload,
                                         action.flags, scheduled.queuedRegion))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
        if (!scheduled.signalSubscriptions.empty())
          obelisk_rt_unregister_signal_wait_unlocked(
              context, scheduled.signalSubscriptions, scheduled.token, false);
        scheduled.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                                 ? UINT64_MAX
                                 : context->schedulerTime + wait->payload;
        // A zero delay resumes in Inactive or Re-Inactive, before the
        // corresponding NBA barrier. The first static-control kernel does not
        // yet model those region queues explicitly, so transfer the complete
        // scheduler state transactionally rather than putting #0 in the
        // ordinary deadline heap and committing NBA too early.
        requestFallback = wait->payload == 0;
      } else {
        if (canUseStaticAOTFanout(context)) {
          if (!scheduled.signalSubscriptions.empty())
            obelisk_rt_unregister_signal_wait_unlocked(
                context, scheduled.signalSubscriptions, scheduled.token, false);
          scheduled.signalLatch.reset();
          break;
        }
        wait = currentWait(scheduled);
        if (!wait)
          return OBELISK_RT_INVALID_FRAME;
        const obelisk_rt_wait_entry_v1 *entries = waitEntries(wait);
        bool sameStaticWait =
            scheduled.signalLatch &&
            scheduled.signalSubscriptions.size() == wait->count;
        for (uint32_t index = 0; sameStaticWait && index != wait->count;
             ++index) {
          const SignalSubscription *subscription =
              scheduled.signalSubscriptions[index].get();
          sameStaticWait =
              subscription &&
              subscription->stableID == entries[index].stable_id &&
              subscription->bitWidth == entries[index].reserved &&
              subscription->edge == entries[index].edge &&
              subscription->target == SignalSubscription::NativeDirectWait;
        }
        if (sameStaticWait) {
          scheduled.signalLatch->triggered = false;
          scheduled.signalLatch->affected = false;
        } else if (!obelisk_rt_register_signal_wait_unlocked(
                       context, wait, scheduled.signalSubscriptions,
                       scheduled.signalLatch, scheduled.token, false)) {
          return context->schedulerStatus;
        }
      }
      break;
    }
    case OBELISK_RT_FRAGMENT_CONTINUE:
      if (!scheduled.signalSubscriptions.empty())
        obelisk_rt_unregister_signal_wait_unlocked(
            context, scheduled.signalSubscriptions, scheduled.token, false);
      scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
      scheduled.waitOffset = 0;
      scheduled.waitSize = 0;
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = false;
      scheduled.queuedRegion = scheduled.homeRegion;
      break;
    case OBELISK_RT_FRAGMENT_TASK_CALL: {
      obelisk_rt_process_instance_v1 *callee = pendingCallee.get();
      if (!callee)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (scheduled.callers.size() == std::numeric_limits<size_t>::max())
        return OBELISK_RT_OUT_OF_RESOURCES;
      scheduled.callers.reserve(scheduled.callers.size() + 1);
      status = context->nativeSchedulePlan->bind(
          context->nativeSchedulePlan->mutable_state, context, actorSlot,
          callee);
      if (status != OBELISK_RT_OK)
        return status;
      scheduled.callers.push_back(selected);
      scheduled.instance = callee;
      scheduled.suspendKind = OBELISK_RT_SUSPEND_NONE;
      scheduled.waitOffset = 0;
      scheduled.waitSize = 0;
      scheduled.waitGenerations.clear();
      scheduled.signalTriggered = false;
      scheduled.urgent = true;
      scheduled.queuedRegion = scheduled.homeRegion;
      context->nativeScheduleActors[actorSlot] = callee;
      context->nativeScheduleActorTokens[actorSlot] = scheduled.token;
      context->nativeScheduleActorIndices[actorSlot] = selectedIndex;
      pendingCallee.release();
      requestFallback = true;
      break;
    }
    default:
      return OBELISK_RT_INVALID_ARGUMENT;
    }

    if (!destroy && !requestFallback &&
        action.kind == OBELISK_RT_FRAGMENT_SUSPEND &&
        action.suspend_kind == OBELISK_RT_SUSPEND_DELAY) {
      setNativeAOTDeadlineUnlocked(context, actorSlot, scheduled.wakeTime);
    } else {
      removeNativeAOTDeadlineUnlocked(context, actorSlot);
      if (!destroy && !requestFallback &&
          action.kind == OBELISK_RT_FRAGMENT_CONTINUE &&
          !markNativeAOTActorReadyUnlocked(context, actorSlot))
        return OBELISK_RT_INVALID_CONTINUATION;
    }
    if (destroy) {
      context->nativeScheduleActors[actorSlot] = nullptr;
      context->nativeScheduleActorTokens[actorSlot] = 0;
      context->nativeScheduleActorIndices[actorSlot] = SIZE_MAX;
    }
    if (terminationRequested) {
      status = refreshNativeAOTReadyPhaseUnlocked(context);
      if (status != OBELISK_RT_OK)
        return status;
    }
    if (context->schedulerSlotProgress == (UINT64_C(1) << 20)) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    ++context->schedulerSlotProgress;
    ++context->signalDiagnostics.aotNodeExecutions;
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
  return requestFallback ? OBELISK_RT_TIER_UNAVAILABLE : OBELISK_RT_OK;
}

class NativeScheduleStepScope {
public:
  NativeScheduleStepScope(obelisk_rt_context *context, uint32_t actorSlot,
                          bool controlOnly)
      : context(context) {
    ContextMutexLock lock(context);
    context->nativeScheduleForcedSlot = actorSlot;
    context->nativeScheduleSingleStep = true;
    context->nativeScheduleForcedExecuted = false;
    context->nativeScheduleControlOnly = controlOnly;
  }

  NativeScheduleStepScope(const NativeScheduleStepScope &) = delete;
  NativeScheduleStepScope &operator=(const NativeScheduleStepScope &) = delete;

  ~NativeScheduleStepScope() {
    ContextMutexLock lock(context);
    context->nativeScheduleForcedSlot = UINT32_MAX;
    context->nativeScheduleSingleStep = false;
    context->nativeScheduleForcedExecuted = false;
    context->nativeScheduleControlOnly = false;
  }

  bool executed() const {
    ContextMutexLock lock(context);
    return context->nativeScheduleForcedExecuted;
  }

private:
  obelisk_rt_context *context;
};

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_run_aot_nodes(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_node *nodes,
    uint32_t nodeCount) {
  if (!context || !nodes || nodeCount == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  {
    ContextMutexLock lock(context);
    if (!context->nativeSchedulePlan || context->nativeScheduleSingleStep ||
        context->nativeScheduleForcedSlot != UINT32_MAX ||
        context->nativeScheduleControlOnly)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_status status =
        initializeNativeAOTNodesUnlocked(context, nodes, nodeCount);
    if (status != OBELISK_RT_OK)
      return status;
  }

  uint32_t nodeCursor = 0;
  bool passProgress = false;
  for (;;) {
    uint32_t selectedNode = UINT32_MAX;
    bool readyBeforeCursor = false;
    {
      ContextMutexLock lock(context);
      uint32_t cursorWord = nodeCursor / 64;
      uint32_t cursorBit = nodeCursor % 64;
      for (uint32_t wordIndex = cursorWord;
           wordIndex != context->nativeScheduleReadyNodes.size(); ++wordIndex) {
        uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
        if (wordIndex == cursorWord && cursorBit != 0)
          word &= UINT64_MAX << cursorBit;
        if (word == 0)
          continue;
        selectedNode =
            wordIndex * 64 + static_cast<uint32_t>(__builtin_ctzll(word));
        break;
      }
      for (uint32_t wordIndex = 0;
           wordIndex <= cursorWord &&
           wordIndex < context->nativeScheduleReadyNodes.size();
           ++wordIndex) {
        uint64_t word = context->nativeScheduleReadyNodes[wordIndex];
        if (wordIndex == cursorWord && cursorBit != 0)
          word &= (uint64_t{1} << cursorBit) - 1;
        if (word != 0) {
          readyBeforeCursor = true;
          break;
        }
      }
      if (selectedNode != UINT32_MAX) {
        const obelisk_rt_native_schedule_node &node =
            context->nativeScheduleNodes[selectedNode];
        if (node.actor_slot >= context->nativeScheduleActors.size() ||
            !context->nativeScheduleActors[node.actor_slot] ||
            context->nativeScheduleActors[node.actor_slot]->continuation !=
                node.continuation)
          return OBELISK_RT_INVALID_CONTINUATION;
        clearNativeAOTNodeReadyUnlocked(context, selectedNode);
      }
    }
    if (selectedNode != UINT32_MAX) {
      uint32_t lastNode = selectedNode;
      bool restartBeforeCursor = false;
      for (;;) {
        const obelisk_rt_native_schedule_node &selected =
            context->nativeScheduleNodes[lastNode];
        {
          ContextMutexLock lock(context);
          context->nativeScheduleMinimumActivatedNode = UINT32_MAX;
        }
        obelisk_rt_status status = executeAOTNode(context, selected.actor_slot);
        if (status != OBELISK_RT_OK)
          return status;
        passProgress = true;

        uint32_t nextNode = lastNode + 1;
        bool fuseNext = false;
        {
          ContextMutexLock lock(context);
          restartBeforeCursor =
              context->nativeScheduleMinimumActivatedNode < nextNode;
          if (!restartBeforeCursor && selected.fusion_group != UINT32_MAX &&
              nextNode < context->nativeScheduleNodes.size() &&
              (!context->nativeSchedulePlan->specialization_fast ||
               *context->nativeSchedulePlan->specialization_fast != 0)) {
            const obelisk_rt_native_schedule_node &next =
                context->nativeScheduleNodes[nextNode];
            uint64_t mask = uint64_t{1} << (nextNode % 64);
            fuseNext =
                next.fusion_group == selected.fusion_group &&
                (context->nativeScheduleReadyNodes[nextNode / 64] & mask) !=
                    0 &&
                next.actor_slot < context->nativeScheduleActors.size() &&
                context->nativeScheduleActors[next.actor_slot] &&
                context->nativeScheduleActors[next.actor_slot]->continuation ==
                    next.continuation;
            if (fuseNext)
              clearNativeAOTNodeReadyUnlocked(context, nextNode);
          }
        }
        if (!fuseNext)
          break;
        lastNode = nextNode;
      }
      nodeCursor = restartBeforeCursor ? 0 : lastNode + 1;
      continue;
    }
    if (passProgress) {
      ContextMutexLock lock(context);
      ++context->signalDiagnostics.aotRegionPasses;
    }
    if (readyBeforeCursor) {
      nodeCursor = 0;
      passProgress = false;
      continue;
    }

    uint64_t previousProgress;
    uint64_t previousTime;
    bool previousFinals;
    bool controlOnly;
    {
      ContextMutexLock lock(context);
      previousProgress = context->schedulerSlotProgress;
      previousTime = context->schedulerTime;
      previousFinals = context->schedulerRunningFinals;
      controlOnly = (context->nativeSchedulePlan->flags &
                     OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL) != 0;
    }
    obelisk_rt_status status;
    bool genericControl = !controlOnly;
    if (controlOnly) {
      status = runStaticAOTControlStep(context);
      if (status == OBELISK_RT_TIER_UNAVAILABLE) {
        genericControl = true;
        NativeScheduleStepScope step(context, UINT32_MAX, true);
        status = runScheduler(context);
      }
    } else {
      // The generic scheduler owns hybrid time/region control, but actor
      // execution must return through executeAOTNode so state-plane handoff
      // and continuation validation remain atomic.
      NativeScheduleStepScope step(context, UINT32_MAX, true);
      status = runScheduler(context);
    }
    bool controlProgress;
    {
      ContextMutexLock lock(context);
      if (genericControl && !markDueNativeAOTDeadlinesUnlocked(context))
        return OBELISK_RT_INVALID_CONTINUATION;
      if (context->schedulerRunningFinals != previousFinals) {
        obelisk_rt_status refresh = refreshNativeAOTReadyPhaseUnlocked(context);
        if (refresh != OBELISK_RT_OK)
          return refresh;
      }
      controlProgress = context->schedulerSlotProgress != previousProgress ||
                        context->schedulerTime != previousTime ||
                        context->schedulerRunningFinals != previousFinals;
    }
    if (status != OBELISK_RT_OK || !controlProgress)
      return status;
    nodeCursor = 0;
    passProgress = false;
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

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_snapshot_aot(
    obelisk_rt_context *context, obelisk_rt_aot_deopt_snapshot *outSnapshot) {
  if (!context || !outSnapshot)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if (!plan || context->nativeScheduleRunning ||
        context->nativeScheduleDeoptimized || context->activeNativeProcess)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (plan->state_bit_count != 0 &&
        (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
         !importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count)))
      return OBELISK_RT_LAYOUT_MISMATCH;
    for (uint32_t root = 0; root != context->nativeScheduleNBARootCount;
         ++root) {
      const obelisk_rt_generated_nba_accumulator_256 *generated =
          context->nativeScheduleNBARoots[root].generated_accumulator;
      if (!generated || !hasGeneratedNBAStages(*generated))
        continue;
      if (obelisk_rt_status status = materializeGeneratedNBAAccumulatorUnlocked(
              context, root, generated->exec_region);
          status != OBELISK_RT_OK)
        return status;
    }

    auto &actors = context->nativeScheduleSnapshotActors;
    auto &nbas = context->nativeScheduleSnapshotNBAs;
    actors.clear();
    nbas.clear();
    actors.reserve(context->nativeScheduleActors.size());
    uint32_t readyCount = 0;
    for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
         ++slot) {
      obelisk_rt_process_instance_v1 *instance =
          context->nativeScheduleActors[slot];
      if (!instance)
        continue;
      size_t index = context->nativeScheduleActorIndices[slot];
      if (index >= context->scheduledProcesses.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      const ScheduledProcess &scheduled = context->scheduledProcesses[index];
      if (scheduled.instance != instance ||
          scheduled.token != context->nativeScheduleActorTokens[slot] ||
          scheduled.aotActorSlot != slot)
        return OBELISK_RT_INVALID_LIFECYCLE;
      bool ready = !scheduled.started ||
                   scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE ||
                   (scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY
                        ? scheduled.wakeTime <= context->schedulerTime
                        : scheduled.signalTriggered);
      readyCount += ready;
      obelisk_rt_fragment_action_v1 action{
          scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
              ? OBELISK_RT_FRAGMENT_CONTINUE
              : OBELISK_RT_FRAGMENT_SUSPEND,
          scheduled.suspendKind,
          instance->continuation,
          scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
              ? OBELISK_RT_FRAGMENT_FLAGS_NONE
              : OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
          scheduled.waitOffset,
          scheduled.waitSize};
      actors.push_back({slot, scheduled.phase, scheduled.scheduleRank,
                        scheduled.queuedRegion, scheduled.insertionSequence,
                        scheduled.wakeTime, scheduled.waitOffset,
                        scheduled.waitSize, action, scheduled.started ? 1u : 0u,
                        ready ? 1u : 0u});
    }

    uint32_t nbaSlot = 0;
    auto appendNBA = [&](uint32_t region, uint64_t sequence, uint64_t dueTime) {
      nbas.push_back({nbaSlot++, region, sequence, dueTime});
    };
    for (const ScheduledNBA &nba : context->scheduledNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledManagedNBA &nba : context->scheduledManagedNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignNBA &nba : context->scheduledDesignNBAs)
      appendNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      appendNBA(event.execRegion, event.sequence, event.dueTime);
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      if (accumulator.valid)
        appendNBA(accumulator.execRegion, accumulator.sequence,
                  context->schedulerTime);
    if (nbas.size() > UINT32_MAX)
      return OBELISK_RT_OUT_OF_RESOURCES;

    *outSnapshot = {sizeof(obelisk_rt_aot_deopt_snapshot),
                    context->schedulerTime,
                    actors.empty() ? nullptr : actors.data(),
                    static_cast<uint32_t>(actors.size()),
                    readyCount,
                    nbas.empty() ? nullptr : nbas.data(),
                    static_cast<uint32_t>(nbas.size()),
                    0,
                    context->nextSchedulerSequence};
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  const obelisk_rt_native_schedule_plan *plan = nullptr;
  bool guardedFanout = false;
  bool specializationFast = false;
  {
    ContextMutexLock lock(context);
    plan = context->nativeSchedulePlan;
    if (!plan)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleRunning)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (context->nativeScheduleDeoptimized)
      return runScheduler(context);
    guardedFanout =
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT) != 0 &&
        !context->nativeScheduleExternalWritePending &&
        !context->nativeScheduleDirtyRootsPresent;
    specializationFast = plan->specialization_fast &&
                         !context->nativeScheduleExternalWritePending &&
                         !context->nativeScheduleDirtyRootsPresent &&
                         nativeStaticSpecializationEnvironmentClean(context);
    if (plan->specialization_fast)
      *plan->specialization_fast = specializationFast ? 1 : 0;
    if (specializationFast && context->signalDiagnosticsEnabled)
      ++context->signalDiagnostics.aotStateFastPaths;
    context->nativeScheduleGuardedFanoutActive = guardedFanout;
    context->nativeScheduleRunning = true;
  }
  obelisk_rt_status status;
  {
    NativeAOTContextScope aotScope(context);
    auto invoke = [&] {
      try {
        return plan->run(plan->mutable_state, context);
      } catch (const std::bad_alloc &) {
        return OBELISK_RT_OUT_OF_MEMORY;
      } catch (...) {
        return OBELISK_RT_INVALID_ARGUMENT;
      }
    };
    if ((plan->flags & OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT) != 0 ||
        guardedFanout || specializationFast) {
      NativeAOTMutexScope mutexScope(context);
      status = invoke();
    } else {
      status = invoke();
    }
  }
  {
    ContextMutexLock lock(context);
    if (plan->specialization_fast)
      *plan->specialization_fast = 0;
    context->nativeScheduleGuardedFanoutActive = false;
    if (specializationFast && status != OBELISK_RT_TIER_UNAVAILABLE &&
        plan->state_bit_count != 0) {
      bool synchronized =
          (!context->nativeScheduleExternalWritePending ||
           reconcileNativeDirtyRootsToPlanesUnlocked(context, plan)) &&
          importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count);
      if (!synchronized)
        status = OBELISK_RT_LAYOUT_MISMATCH;
    }
    context->nativeScheduleRunning = false;
  }
  if (status != OBELISK_RT_TIER_UNAVAILABLE)
    return status;

  {
    ContextMutexLock lock(context);
    if (plan->state_bit_count != 0 &&
        (!reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) ||
         !importNativeStatePlanesUnlocked(context, plan->state_value,
                                          plan->state_unknown,
                                          plan->state_bit_count)))
      return OBELISK_RT_LAYOUT_MISMATCH;
  }
  obelisk_rt_aot_deopt_snapshot snapshot{};
  status = plan->fallback_snapshot(plan->mutable_state, context, &snapshot);
  if (status != OBELISK_RT_OK)
    return status;
  {
    ContextMutexLock lock(context);
    uint32_t liveActors = 0;
    for (obelisk_rt_process_instance_v1 *actor : context->nativeScheduleActors)
      liveActors += actor != nullptr;
    uint64_t pendingNBAs = context->scheduledNBAs.size();
    pendingNBAs += context->scheduledManagedNBAs.size();
    pendingNBAs += context->scheduledDesignNBAs.size();
    pendingNBAs += context->scheduledDesignEvents.size();
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      pendingNBAs += accumulator.valid;
    bool valid = snapshot.size == sizeof(obelisk_rt_aot_deopt_snapshot) &&
                 snapshot.current_time == context->schedulerTime &&
                 snapshot.actor_count == liveActors &&
                 (snapshot.actor_count == 0 || snapshot.actors) &&
                 pendingNBAs <= UINT32_MAX &&
                 snapshot.nba_count == pendingNBAs &&
                 (snapshot.nba_count == 0 || snapshot.nbas) &&
                 snapshot.ready_count <= snapshot.actor_count &&
                 snapshot.reserved == 0 &&
                 snapshot.next_sequence == context->nextSchedulerSequence;
    uint32_t readyCount = 0;
    uint32_t previousSlot = UINT32_MAX;
    for (uint32_t index = 0; valid && index != snapshot.actor_count; ++index) {
      const obelisk_rt_aot_deopt_actor &actor = snapshot.actors[index];
      if (actor.slot >= context->nativeScheduleActors.size() ||
          (index != 0 && actor.slot <= previousSlot)) {
        valid = false;
        break;
      }
      previousSlot = actor.slot;
      size_t processIndex = context->nativeScheduleActorIndices[actor.slot];
      obelisk_rt_process_instance_v1 *instance =
          context->nativeScheduleActors[actor.slot];
      if (!instance || processIndex >= context->scheduledProcesses.size()) {
        valid = false;
        break;
      }
      const ScheduledProcess &scheduled =
          context->scheduledProcesses[processIndex];
      bool ready = !scheduled.started ||
                   scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE ||
                   (scheduled.suspendKind == OBELISK_RT_SUSPEND_DELAY
                        ? scheduled.wakeTime <= context->schedulerTime
                        : scheduled.signalTriggered);
      readyCount += ready;
      valid =
          scheduled.instance == instance &&
          scheduled.aotActorSlot == actor.slot &&
          actor.flags == scheduled.phase &&
          actor.schedule_rank == scheduled.scheduleRank &&
          actor.queued_region == scheduled.queuedRegion &&
          actor.insertion_sequence == scheduled.insertionSequence &&
          actor.wake_time == scheduled.wakeTime &&
          actor.wait_offset == scheduled.waitOffset &&
          actor.wait_size == scheduled.waitSize &&
          actor.started == (scheduled.started ? 1u : 0u) &&
          actor.ready == (ready ? 1u : 0u) &&
          actor.action.kind == (scheduled.suspendKind == OBELISK_RT_SUSPEND_NONE
                                    ? OBELISK_RT_FRAGMENT_CONTINUE
                                    : OBELISK_RT_FRAGMENT_SUSPEND) &&
          actor.action.suspend_kind == scheduled.suspendKind &&
          actor.action.continuation == instance->continuation &&
          actor.action.payload == scheduled.waitOffset &&
          actor.action.auxiliary == scheduled.waitSize;
    }
    valid &= readyCount == snapshot.ready_count;
    uint32_t nbaIndex = 0;
    auto validateNBA = [&](uint32_t region, uint64_t sequence,
                           uint64_t dueTime) {
      if (!valid || nbaIndex >= snapshot.nba_count)
        return;
      const obelisk_rt_aot_deopt_nba &nba = snapshot.nbas[nbaIndex];
      valid = nba.slot == nbaIndex && nba.exec_region == region &&
              nba.sequence == sequence && nba.due_time == dueTime;
      ++nbaIndex;
    };
    for (const ScheduledNBA &nba : context->scheduledNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledManagedNBA &nba : context->scheduledManagedNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignNBA &nba : context->scheduledDesignNBAs)
      validateNBA(nba.execRegion, nba.sequence, nba.dueTime);
    for (const ScheduledDesignEvent &event : context->scheduledDesignEvents)
      validateNBA(event.execRegion, event.sequence, event.dueTime);
    for (const StaticNBAAccumulator &accumulator :
         context->staticNBAAccumulators)
      if (accumulator.valid)
        validateNBA(accumulator.execRegion, accumulator.sequence,
                    context->schedulerTime);
    valid &= nbaIndex == snapshot.nba_count;
    if (!valid)
      return OBELISK_RT_INVALID_ARGUMENT;
    context->nativeScheduleDeoptimized = true;
    rebuildNativeSchedulerIndexUnlocked(context);
    ++context->signalDiagnostics.aotFallbacks;
  }
  return runScheduler(context);
}

void obelisk_rt_release_native_schedule_plan(
    obelisk_rt_context *context) noexcept {
  if (!context || !context->nativeSchedulePlan)
    return;
  try {
    if (context->nativeSchedulePlan->specialization_fast)
      *context->nativeSchedulePlan->specialization_fast = 0;
    for (uint32_t root = 0; root != context->nativeScheduleNBARootCount; ++root)
      if (context->nativeScheduleNBARoots[root].generated_accumulator)
        *context->nativeScheduleNBARoots[root].generated_accumulator = {};
    for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
         ++slot)
      if (context->nativeScheduleActors[slot])
        (void)context->nativeSchedulePlan->bind(
            context->nativeSchedulePlan->mutable_state, context, slot, nullptr);
    std::lock_guard<std::mutex> lock(nativeScheduleRegistryMutex);
    installedNativeScheduleStates.erase(
        context->nativeSchedulePlan->mutable_state);
  } catch (...) {
  }
  context->nativeSchedulePlan = nullptr;
  context->nativeScheduleNBARoots = nullptr;
  context->nativeScheduleNBARootCount = 0;
  context->nativeScheduleNBASites = nullptr;
  context->nativeScheduleNBASiteCount = 0;
  context->nativeScheduleFanoutEntries = nullptr;
  context->nativeScheduleFanoutEntryCount = 0;
  context->nativeScheduleActorRoots = nullptr;
  context->nativeScheduleActorRootCount = 0;
  context->nativeScheduleNBASiteIndex.clear();
  context->nativeScheduleActors.clear();
  context->nativeScheduleActorTokens.clear();
  context->nativeScheduleActorIndices.clear();
  context->nativeScheduleNodes.clear();
  context->nativeScheduleActorNodes.clear();
  context->nativeScheduleReadyNodes.clear();
  context->nativeScheduleDeadlines.clear();
  context->nativeScheduleDeadlineHeap.clear();
  context->nativeScheduleDeadlinePositions.clear();
  context->nativeScheduleStaticStateIndex.clear();
  context->nativeScheduleSnapshotActors.clear();
  context->nativeScheduleSnapshotNBAs.clear();
  context->staticNBAAccumulators.clear();
  context->staticNBASlowRoots.clear();
  context->staticNBARootHasFanout.clear();
  context->nativeScheduleTransientDirtyRoots.clear();
  context->nativeSchedulePersistentDirtyRoots.clear();
  context->nativeScheduleTransientDirtyMask.clear();
  context->nativeSchedulePersistentDirtyMask.clear();
  context->nativeScheduleDirtyRootsPresent = false;
  context->nativeScheduleAVX2 = false;
  context->nativeScheduleGuardedFanoutActive = false;
  context->nativeScheduleForcedSlot = UINT32_MAX;
  context->nativeScheduleSingleStep = false;
  context->nativeScheduleForcedExecuted = false;
  context->nativeScheduleControlOnly = false;
}

void obelisk_rt_aot_external_write_unlocked(obelisk_rt_context *context) {
  if (!context || !context->nativeSchedulePlan ||
      context->nativeScheduleDeoptimized)
    return;
  if (context->nativeSchedulePlan->specialization_fast)
    *context->nativeSchedulePlan->specialization_fast = 0;
  context->nativeScheduleExternalWritePending = true;
  for (uint32_t slot = 0; slot != context->nativeScheduleActors.size();
       ++slot) {
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    bool nativeRootBootstrap =
        slot == 0 && (context->nativeSchedulePlan->flags &
                      OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO) != 0;
    if (!nativeRootBootstrap && nativeAOTActorDirty(context, slot) && actor &&
        actor->lifecycle != OBELISK_RT_PROCESS_TERMINATED &&
        (actor->descriptor->available_tiers & OBELISK_RT_TIER_MASK_BYTECODE) !=
            0)
      actor->tier = OBELISK_RT_TIER_BYTECODE;
  }
}

void obelisk_rt_aot_external_write_range_unlocked(obelisk_rt_context *context,
                                                  uint64_t bitOffset,
                                                  uint64_t bitWidth,
                                                  bool persistent) {
  if (!context || bitWidth == 0)
    return;
  auto &dirty = persistent ? context->nativeSchedulePersistentDirtyRoots
                           : context->nativeScheduleTransientDirtyRoots;
  auto &dirtyMask = persistent ? context->nativeSchedulePersistentDirtyMask
                               : context->nativeScheduleTransientDirtyMask;
  __int128 dirtyEnd = static_cast<__int128>(bitOffset) + bitWidth;
  for (const auto &[id, state] : context->nativeStaticStates)
    if (static_cast<__int128>(state.bitOffset) < dirtyEnd &&
        static_cast<__int128>(bitOffset) <
            static_cast<__int128>(state.bitOffset) + state.bitWidth) {
      dirty.insert(id);
      if (id < dirtyMask.size())
        dirtyMask[id] = 1;
      context->nativeScheduleDirtyRootsPresent = true;
    }
  obelisk_rt_aot_external_write_unlocked(context);
}

void obelisk_rt_aot_release_range_unlocked(obelisk_rt_context *context,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth) {
  if (!context || bitWidth == 0)
    return;
  __int128 releasedEnd = static_cast<__int128>(bitOffset) + bitWidth;
  for (const auto &[id, state] : context->nativeStaticStates)
    if (static_cast<__int128>(state.bitOffset) < releasedEnd &&
        static_cast<__int128>(bitOffset) <
            static_cast<__int128>(state.bitOffset) + state.bitWidth) {
      if (nativeMaskIntersectsRange(context->forceMask, state.bitOffset,
                                    state.bitWidth) ||
          nativeMaskIntersectsRange(context->assignMask, state.bitOffset,
                                    state.bitWidth))
        continue;
      if (context->nativeSchedulePlan && !context->nativeScheduleDeoptimized &&
          !reconcileNativeRootToPlanesUnlocked(
              context, context->nativeSchedulePlan, id)) {
        if (context->schedulerStatus == OBELISK_RT_OK)
          context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
        continue;
      }
      context->nativeSchedulePersistentDirtyRoots.erase(id);
      if (id < context->nativeSchedulePersistentDirtyMask.size())
        context->nativeSchedulePersistentDirtyMask[id] = 0;
    }
  context->nativeScheduleDirtyRootsPresent =
      !context->nativeScheduleTransientDirtyRoots.empty() ||
      !context->nativeSchedulePersistentDirtyRoots.empty();
  refreshNativeStaticSpecializationFastUnlocked(context);
}

extern "C" uint32_t obelisk_rt_v1_static_specialization_guard(
    obelisk_rt_context *context, uint32_t actorSlot, uint32_t staticState,
    uint32_t flags) {
  if (!context || !context->nativeSchedulePlan ||
      (actorSlot != UINT32_MAX &&
       actorSlot >= context->nativeScheduleActors.size()) ||
      staticState == 0 || flags == 0 ||
      (flags & ~(OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE)) !=
          0)
    return 0;
  auto dirty = [&](const std::vector<uint8_t> &mask,
                   const std::unordered_set<uint32_t> &sparse) {
    return staticState < mask.size() ? mask[staticState] != 0
                                     : sparse.find(staticState) != sparse.end();
  };
  auto record = [&](bool fast) {
    if (!context->signalDiagnosticsEnabled)
      return;
    if (fast)
      ++context->signalDiagnostics.aotStateFastPaths;
    else
      ++context->signalDiagnostics.aotStateSlowPaths;
  };
  if (context->nativeScheduleDeoptimized ||
      dirty(context->nativeScheduleTransientDirtyMask,
            context->nativeScheduleTransientDirtyRoots) ||
      dirty(context->nativeSchedulePersistentDirtyMask,
            context->nativeSchedulePersistentDirtyRoots)) {
    record(false);
    return 0;
  }
  if (actorSlot == UINT32_MAX) {
    bool allowed = findNativeStaticState(context, staticState) != nullptr;
    record(allowed);
    return allowed;
  }
  for (uint64_t index = 0; index != context->nativeScheduleActorRootCount;
       ++index) {
    const obelisk_rt_static_actor_root &dependency =
        context->nativeScheduleActorRoots[index];
    if (dependency.actor_slot == actorSlot &&
        dependency.static_state == staticState &&
        (dependency.flags & flags) == flags) {
      record(true);
      return 1;
    }
  }
  record(false);
  return 0;
}

extern "C" uint32_t
obelisk_rt_v1_static_nba_specialization_guard(obelisk_rt_context *context,
                                              uint32_t rootIndex) {
  if (!context || activeNativeAOTContext != context ||
      !context->nativeSchedulePlan || context->nativeScheduleDeoptimized ||
      rootIndex >= context->nativeScheduleNBARootCount ||
      rootIndex >= context->staticNBASlowRoots.size())
    return 0;
  const obelisk_rt_static_nba_root &root =
      context->nativeScheduleNBARoots[rootIndex];
  return context->staticNBASlowRoots[rootIndex] == 0 &&
                 !nativeStaticRootDirty(context, root.static_state)
             ? 1
             : 0;
}
