//===- ManagedHeap.cpp - Precise SystemVerilog class heap ----------------===//

#include "RuntimeInternal.h"

#include <array>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>

struct obelisk_rt_object_v1 {};

namespace {

constexpr uint64_t kObjectMagic = UINT64_C(0x4f424a4543545631);
constexpr uint64_t kRootCookie = UINT64_C(0x524f4f545f56315a);
constexpr uint64_t kProviderCookie = UINT64_C(0x50524f565f56315a);
constexpr uint64_t kChunkSize = 2 * 1024 * 1024;
constexpr uint64_t kSpanSize = 64 * 1024;
constexpr uint64_t kChunkAlignment = 64;
constexpr uint64_t kSpansPerChunk = kChunkSize / kSpanSize;
constexpr uint64_t kMinimumCollectionBytes = 32 * 1024 * 1024;
constexpr uint64_t kMaximumSmallSlot = 32 * 1024;
constexpr unsigned kSizeClassCount = 11; // 32 bytes through 32 KiB.
constexpr size_t kEmptyChunkCache = 2;

static_assert(kSpansPerChunk == 32);

enum class LaneState : uint32_t {
  Inactive,
  Active,
  Parked,
  Collecting,
  Destroying
};

struct Span;
struct LargeAllocation;

struct ObjectMetadata {
  class ManagedHeap *heap = nullptr;
  void *object = nullptr;
  const obelisk_rt_class_descriptor_v1 *descriptor = nullptr;
  uint64_t identity = 0;
  std::atomic<uint32_t> pins{0};
  std::atomic<uint32_t> nextTicket{0};
  std::atomic<uint32_t> servingTicket{0};
  std::atomic<bool> allocated{false};
  bool marked = false;
};

static_assert(sizeof(ObjectMetadata) <= 64,
              "small-object metadata must remain cache friendly");

struct SlotPrefix {
  ObjectMetadata *metadata;
  uint64_t magic;
};

static_assert(sizeof(SlotPrefix) == 16);

struct Span {
  uint8_t *memory = nullptr;
  uint32_t classIndex = UINT32_MAX;
  uint32_t slotSize = 0;
  uint32_t capacity = 0;
  uint32_t bump = 0;
  uint32_t freeHead = UINT32_MAX;
  uint32_t live = 0;
  std::unique_ptr<ObjectMetadata[]> metadata;

  bool assigned() const { return classIndex != UINT32_MAX; }
  bool hasSpace() const { return freeHead != UINT32_MAX || bump < capacity; }
};

struct Chunk {
  uint8_t *memory = nullptr;
  std::array<Span, kSpansPerChunk> spans;

  Chunk() {
    memory = static_cast<uint8_t *>(
        ::operator new(kChunkSize, std::align_val_t(kChunkAlignment)));
    std::memset(memory, 0, kChunkSize);
    for (size_t index = 0; index != spans.size(); ++index)
      spans[index].memory = memory + index * kSpanSize;
  }

  ~Chunk() { ::operator delete(memory, std::align_val_t(kChunkAlignment)); }

  bool empty() const {
    for (const Span &span : spans)
      if (span.live != 0)
        return false;
    return true;
  }
};

struct LargeAllocation {
  uint8_t *storage = nullptr;
  ObjectMetadata metadata;

  explicit LargeAllocation(uint64_t allocationSize) {
    storage = static_cast<uint8_t *>(
        ::operator new(allocationSize, std::align_val_t(16)));
    std::memset(storage, 0, allocationSize);
  }

  ~LargeAllocation() { ::operator delete(storage, std::align_val_t(16)); }
};

uint64_t roundUp(uint64_t value, uint64_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

bool validPowerOfTwo(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

unsigned sizeClassFor(uint64_t size, uint32_t &classSize) {
  classSize = 32;
  unsigned index = 0;
  while (classSize < size && index + 1 < kSizeClassCount) {
    classSize <<= 1;
    ++index;
  }
  return index;
}

ObjectMetadata *metadataFor(const obelisk_rt_object_v1 *object) {
  if (!object)
    return nullptr;
  const auto *bytes = reinterpret_cast<const uint8_t *>(object);
  const auto *prefix =
      reinterpret_cast<const SlotPrefix *>(bytes - sizeof(SlotPrefix));
  if (prefix->magic != kObjectMagic || !prefix->metadata)
    return nullptr;
  ObjectMetadata *metadata = prefix->metadata;
  if (metadata->object != object ||
      !metadata->allocated.load(std::memory_order_acquire))
    return nullptr;
  return metadata;
}

class ObjectLock {
public:
  explicit ObjectLock(ObjectMetadata *metadata) : metadata(metadata) {
    ticket = metadata->nextTicket.fetch_add(1, std::memory_order_relaxed);
    while (metadata->servingTicket.load(std::memory_order_acquire) != ticket)
      std::this_thread::yield();
  }

  ~ObjectLock() {
    metadata->servingTicket.store(ticket + 1, std::memory_order_release);
  }

private:
  ObjectMetadata *metadata;
  uint32_t ticket = 0;
};

bool checkedRange(uint64_t offset, uint64_t size, uint64_t extent) {
  return offset <= extent && size <= extent - offset;
}

struct TraceValidationFrame {
  const obelisk_rt_trace_layout_v1 *layout;
  const TraceValidationFrame *parent;
};

bool validateTraceLayout(const obelisk_rt_trace_layout_v1 *layout,
                         uint64_t containingSize,
                         const TraceValidationFrame *parent = nullptr) {
  for (const TraceValidationFrame *frame = parent; frame; frame = frame->parent)
    if (frame->layout == layout)
      return false;
  if (!layout || layout->version != OBELISK_RT_VERSION ||
      layout->reserved != 0 || layout->size > containingSize ||
      !validPowerOfTwo(layout->alignment) || layout->alignment > 16 ||
      (layout->entry_count == 0) != (layout->entries == nullptr))
    return false;

  TraceValidationFrame frame{layout, parent};
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    if (entry.reserved != 0 || entry.count == 0 ||
        (entry.count > 1 && entry.stride == 0))
      return false;
    uint64_t elementSize = sizeof(obelisk_rt_object_v1 *);
    if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
      if (!entry.child_layout)
        return false;
      elementSize = entry.child_layout->size;
    } else if ((entry.kind != OBELISK_RT_TRACE_STRONG &&
                entry.kind != OBELISK_RT_TRACE_WEAK) ||
               entry.child_layout)
      return false;
    uint64_t requiredAlignment = entry.kind == OBELISK_RT_TRACE_EMBEDDED
                                     ? entry.child_layout->alignment
                                     : alignof(obelisk_rt_object_v1 *);
    if (entry.offset % requiredAlignment != 0 ||
        (entry.count > 1 && entry.stride % requiredAlignment != 0))
      return false;
    if (entry.count - 1 >
        (std::numeric_limits<uint64_t>::max() - entry.offset) /
            std::max<uint64_t>(entry.stride, 1))
      return false;
    uint64_t last = entry.offset + (entry.count - 1) * entry.stride;
    if (!checkedRange(last, elementSize, layout->size) ||
        (entry.kind == OBELISK_RT_TRACE_EMBEDDED &&
         !validateTraceLayout(entry.child_layout, elementSize, &frame)))
      return false;
  }
  return true;
}

bool layoutHasHandleAt(const obelisk_rt_trace_layout_v1 *layout,
                       uint64_t baseOffset, uint64_t wantedOffset) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (layoutHasHandleAt(entry.child_layout, offset, wantedOffset))
          return true;
      } else if (offset == wantedOffset) {
        return true;
      }
    }
  }
  return false;
}

bool layoutHasWeakHandleAt(const obelisk_rt_trace_layout_v1 *layout,
                           uint64_t baseOffset, uint64_t wantedOffset) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (layoutHasWeakHandleAt(entry.child_layout, offset, wantedOffset))
          return true;
      } else if (entry.kind == OBELISK_RT_TRACE_WEAK &&
                 offset == wantedOffset) {
        return true;
      }
    }
  }
  return false;
}

bool layoutHasStrongHandleAt(const obelisk_rt_trace_layout_v1 *layout,
                             uint64_t baseOffset, uint64_t wantedOffset) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (layoutHasStrongHandleAt(entry.child_layout, offset, wantedOffset))
          return true;
      } else if (entry.kind == OBELISK_RT_TRACE_STRONG &&
                 offset == wantedOffset) {
        return true;
      }
    }
  }
  return false;
}

unsigned layoutHandleCountAt(const obelisk_rt_trace_layout_v1 *layout,
                             uint64_t baseOffset, uint64_t wantedOffset) {
  if (!layout)
    return 0;
  unsigned count = 0;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED)
        count += layoutHandleCountAt(entry.child_layout, offset, wantedOffset);
      else if (offset == wantedOffset)
        ++count;
      if (count > 1)
        return count;
    }
  }
  return count;
}

bool layoutHandlesAreUnique(const obelisk_rt_trace_layout_v1 *root,
                            const obelisk_rt_trace_layout_v1 *layout,
                            uint64_t baseOffset = 0) {
  if (!layout)
    return true;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (!layoutHandlesAreUnique(root, entry.child_layout, offset))
          return false;
      } else if (layoutHandleCountAt(root, 0, offset) != 1) {
        return false;
      }
    }
  }
  return true;
}

bool layoutContainsHandles(const obelisk_rt_trace_layout_v1 *candidate,
                           const obelisk_rt_trace_layout_v1 *required,
                           uint64_t baseOffset = 0) {
  if (!required)
    return true;
  if (!candidate)
    return false;
  for (uint64_t index = 0; index != required->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = required->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (!layoutContainsHandles(candidate, entry.child_layout, offset))
          return false;
      } else if (entry.kind == OBELISK_RT_TRACE_STRONG) {
        if (!layoutHasStrongHandleAt(candidate, 0, offset))
          return false;
      } else if (!layoutHasWeakHandleAt(candidate, 0, offset)) {
        return false;
      }
    }
  }
  return true;
}

bool layoutOverlapsHandle(const obelisk_rt_trace_layout_v1 *layout,
                          uint64_t baseOffset, uint64_t offset, uint64_t size) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t fieldOffset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (layoutOverlapsHandle(entry.child_layout, fieldOffset, offset, size))
          return true;
        continue;
      }
      uint64_t fieldEnd = fieldOffset + sizeof(obelisk_rt_object_v1 *);
      uint64_t end = offset + size;
      if (offset < fieldEnd && fieldOffset < end)
        return true;
    }
  }
  return false;
}

bool validateLayoutHandleWrite(
    const obelisk_rt_trace_layout_v1 *layout, uint64_t baseOffset,
    uint64_t offset, uint64_t size, const uint8_t *data, ManagedHeap *heap,
    std::vector<obelisk_rt_object_v1 *> *referents = nullptr) {
  if (!layout)
    return true;
  uint64_t end = offset + size;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t fieldOffset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (!validateLayoutHandleWrite(entry.child_layout, fieldOffset, offset,
                                       size, data, heap, referents))
          return false;
        continue;
      }
      uint64_t fieldEnd = fieldOffset + sizeof(obelisk_rt_object_v1 *);
      if (offset >= fieldEnd || fieldOffset >= end)
        continue;
      // Never admit a torn managed pointer, even from a trusted generated
      // aggregate store.
      if (fieldOffset < offset || fieldEnd > end)
        return false;
      obelisk_rt_object_v1 *referent = nullptr;
      std::memcpy(&referent, data + fieldOffset - offset, sizeof(referent));
      if (!referent)
        continue;
      ObjectMetadata *referentMetadata = metadataFor(referent);
      if (!referentMetadata || referentMetadata->heap != heap)
        return false;
      if (referents)
        referents->push_back(referent);
    }
  }
  return true;
}

} // namespace

struct obelisk_rt_gc_lane_v1 {
  ManagedHeap *heap = nullptr;
  obelisk_rt_context *context = nullptr;
  std::atomic<uintptr_t> owner{0};
  std::atomic<LaneState> state{LaneState::Inactive};
  std::atomic<obelisk_rt_gc_root_v1 *> roots{nullptr};
  std::atomic<obelisk_rt_gc_root_range_v1 *> rootRanges{nullptr};
  std::atomic<ManagedRootProvider *> providers{nullptr};
};

namespace {

struct ThreadAllocationCache {
  ManagedHeap *heap = nullptr;
  uint64_t heapID = 0;
  uint64_t epoch = 0;
  std::array<Span *, kSizeClassCount> spans{};
  std::array<const obelisk_rt_class_descriptor_v1 *, 8> validatedDescriptors{};
  uint32_t nextValidatedDescriptor = 0;
};

thread_local std::vector<ThreadAllocationCache> allocationCaches;
thread_local const char laneOwnerToken = 0;
std::atomic<uint64_t> nextHeapID{1};

uintptr_t currentLaneOwnerToken() {
  return reinterpret_cast<uintptr_t>(&laneOwnerToken);
}

std::optional<uint64_t>
acquireMonotonicIdentity(std::atomic<uint64_t> &source) {
  uint64_t current = source.load(std::memory_order_relaxed);
  while (current != 0) {
    uint64_t next =
        current == std::numeric_limits<uint64_t>::max() ? 0 : current + 1;
    if (source.compare_exchange_weak(current, next, std::memory_order_relaxed))
      return current;
  }
  return std::nullopt;
}

uint64_t acquireHeapIdentity() {
  std::optional<uint64_t> identity = acquireMonotonicIdentity(nextHeapID);
  if (!identity)
    throw std::bad_alloc();
  return *identity;
}

ThreadAllocationCache &allocationCache(ManagedHeap *heap, uint64_t heapID,
                                       uint64_t epoch) {
  for (ThreadAllocationCache &cache : allocationCaches) {
    if (cache.heap != heap || cache.heapID != heapID)
      continue;
    if (cache.epoch != epoch) {
      cache.epoch = epoch;
      cache.spans.fill(nullptr);
    }
    return cache;
  }
  allocationCaches.push_back({});
  ThreadAllocationCache &cache = allocationCaches.back();
  cache.heap = heap;
  cache.heapID = heapID;
  cache.epoch = epoch;
  return cache;
}

} // namespace

class ManagedHeap {
public:
  explicit ManagedHeap(obelisk_rt_context *context)
      : context(context), id(acquireHeapIdentity()) {}

  ~ManagedHeap() {
    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      delete lane;
  }

  obelisk_rt_status createLane(obelisk_rt_gc_lane_v1 **outLane) {
    if (!outLane)
      return OBELISK_RT_INVALID_ARGUMENT;
    *outLane = nullptr;
    auto lane = std::make_unique<obelisk_rt_gc_lane_v1>();
    lane->heap = this;
    lane->context = context;
    {
      std::lock_guard<std::mutex> worldLock(worldMutex);
      std::lock_guard<std::mutex> laneLock(laneMutex);
      lanes.push_back(lane.get());
    }
    *outLane = lane.release();
    return OBELISK_RT_OK;
  }

  obelisk_rt_status destroyLane(obelisk_rt_gc_lane_v1 *lane) {
    if (!owns(lane))
      return OBELISK_RT_INVALID_ARGUMENT;
    LaneState expected = LaneState::Inactive;
    if (!lane->state.compare_exchange_strong(expected, LaneState::Destroying,
                                             std::memory_order_acq_rel))
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (lane->roots.load(std::memory_order_acquire) ||
        lane->rootRanges.load(std::memory_order_acquire) ||
        lane->providers.load(std::memory_order_acquire)) {
      lane->state.store(LaneState::Inactive, std::memory_order_release);
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
    {
      std::lock_guard<std::mutex> worldLock(worldMutex);
      std::lock_guard<std::mutex> laneLock(laneMutex);
      auto found = std::find(lanes.begin(), lanes.end(), lane);
      if (found == lanes.end()) {
        lane->state.store(LaneState::Inactive, std::memory_order_release);
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      lanes.erase(found);
    }
    delete lane;
    return OBELISK_RT_OK;
  }

  obelisk_rt_status enter(obelisk_rt_gc_lane_v1 *lane) {
    if (!owns(lane))
      return OBELISK_RT_INVALID_ARGUMENT;
    LaneState expected = LaneState::Inactive;
    if (!lane->state.compare_exchange_strong(expected, LaneState::Active,
                                             std::memory_order_acq_rel))
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->owner.store(currentLaneOwnerToken(), std::memory_order_release);
    return safepoint(lane);
  }

  obelisk_rt_status leave(obelisk_rt_gc_lane_v1 *lane) {
    if (!activeOwner(lane))
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_status status = safepoint(lane);
    if (status != OBELISK_RT_OK)
      return status;
    if (lane->roots.load(std::memory_order_acquire) ||
        lane->rootRanges.load(std::memory_order_acquire) ||
        lane->providers.load(std::memory_order_acquire))
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->owner.store(0, std::memory_order_release);
    lane->state.store(LaneState::Inactive, std::memory_order_release);
    worldCondition.notify_all();
    return OBELISK_RT_OK;
  }

  obelisk_rt_status safepoint(obelisk_rt_gc_lane_v1 *lane) {
    if (!activeOwner(lane))
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!collectionRequested.load(std::memory_order_acquire))
      return OBELISK_RT_OK;
    std::unique_lock<std::mutex> lock(worldMutex);
    if (!collectionRequested.load(std::memory_order_acquire))
      return OBELISK_RT_OK;
    lane->state.store(LaneState::Parked, std::memory_order_release);
    worldCondition.notify_all();
    worldCondition.wait(lock, [&] {
      return !collectionRequested.load(std::memory_order_acquire);
    });
    lane->state.store(LaneState::Active, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status pushRoot(obelisk_rt_gc_lane_v1 *lane,
                             obelisk_rt_gc_root_v1 *root,
                             obelisk_rt_object_v1 **slot) {
    if (!activeOwner(lane) || !root || !slot || root->cookie != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    root->slot = slot;
    root->previous = lane->roots.load(std::memory_order_relaxed);
    root->cookie = kRootCookie ^ reinterpret_cast<uintptr_t>(lane);
    lane->roots.store(root, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status popRoot(obelisk_rt_gc_lane_v1 *lane,
                            obelisk_rt_gc_root_v1 *root) {
    if (!activeOwner(lane) || !root ||
        root->cookie != (kRootCookie ^ reinterpret_cast<uintptr_t>(lane)) ||
        lane->roots.load(std::memory_order_acquire) != root)
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->roots.store(root->previous, std::memory_order_release);
    root->slot = nullptr;
    root->previous = nullptr;
    root->cookie = 0;
    return OBELISK_RT_OK;
  }

  obelisk_rt_status pushRootRange(obelisk_rt_gc_lane_v1 *lane,
                                  obelisk_rt_gc_root_range_v1 *range,
                                  obelisk_rt_object_v1 **slots,
                                  uint64_t count) {
    if (!activeOwner(lane) || !range || (!slots && count != 0) ||
        range->cookie != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    range->slots = slots;
    range->count = count;
    range->previous = lane->rootRanges.load(std::memory_order_relaxed);
    range->cookie = kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
                    reinterpret_cast<uintptr_t>(range);
    lane->rootRanges.store(range, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status popRootRange(obelisk_rt_gc_lane_v1 *lane,
                                 obelisk_rt_gc_root_range_v1 *range) {
    if (!activeOwner(lane) || !range ||
        range->cookie != (kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
                          reinterpret_cast<uintptr_t>(range)) ||
        lane->rootRanges.load(std::memory_order_acquire) != range)
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->rootRanges.store(range->previous, std::memory_order_release);
    range->slots = nullptr;
    range->count = 0;
    range->previous = nullptr;
    range->cookie = 0;
    return OBELISK_RT_OK;
  }

  obelisk_rt_status pushProvider(obelisk_rt_gc_lane_v1 *lane,
                                 ManagedRootProvider *provider,
                                 ManagedRootEnumerate enumerate,
                                 void *environment) {
    if (!activeOwner(lane) || !provider || !enumerate || provider->previous ||
        provider->cookie != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    provider->enumerate = enumerate;
    provider->environment = environment;
    provider->previous = lane->providers.load(std::memory_order_relaxed);
    provider->cookie = kProviderCookie ^ reinterpret_cast<uintptr_t>(lane);
    lane->providers.store(provider, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status popProvider(obelisk_rt_gc_lane_v1 *lane,
                                ManagedRootProvider *provider) {
    if (!activeOwner(lane) || !provider ||
        provider->cookie !=
            (kProviderCookie ^ reinterpret_cast<uintptr_t>(lane)) ||
        lane->providers.load(std::memory_order_acquire) != provider)
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->providers.store(provider->previous, std::memory_order_release);
    provider->enumerate = nullptr;
    provider->environment = nullptr;
    provider->previous = nullptr;
    provider->cookie = 0;
    return OBELISK_RT_OK;
  }

  obelisk_rt_status registerStatic(obelisk_rt_object_v1 **slot,
                                   bool activeCaller) {
    if (!slot)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> worldLock(worldMutex, std::defer_lock);
    if (!activeCaller)
      worldLock.lock();
    std::lock_guard<std::mutex> rootLock(rootMutex);
    if (std::find(staticRoots.begin(), staticRoots.end(), slot) !=
        staticRoots.end())
      return OBELISK_RT_INVALID_LIFECYCLE;
    staticRoots.push_back(slot);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status unregisterStatic(obelisk_rt_object_v1 **slot,
                                     bool activeCaller) {
    if (!slot)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> worldLock(worldMutex, std::defer_lock);
    if (!activeCaller)
      worldLock.lock();
    std::lock_guard<std::mutex> rootLock(rootMutex);
    auto found = std::find(staticRoots.begin(), staticRoots.end(), slot);
    if (found == staticRoots.end())
      return OBELISK_RT_INVALID_ARGUMENT;
    staticRoots.erase(found);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status pin(obelisk_rt_object_v1 *object, bool activeCaller) {
    // Pins are external roots. Serialize their publication with the
    // stop-the-world snapshot so a successful pin is either visible to the
    // current collection or begins after that collection has completed. An
    // active lane already excludes the collector until its next safepoint and
    // must not acquire worldMutex while the collector may be waiting for it.
    std::unique_lock<std::mutex> worldLock(worldMutex, std::defer_lock);
    if (!activeCaller)
      worldLock.lock();
    ObjectMetadata *metadata = metadataFor(object);
    if (!metadata || metadata->heap != this)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t current = metadata->pins.load(std::memory_order_relaxed);
    do {
      if (current == UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
    } while (!metadata->pins.compare_exchange_weak(current, current + 1,
                                                   std::memory_order_acq_rel));
    return OBELISK_RT_OK;
  }

  obelisk_rt_status unpin(obelisk_rt_object_v1 *object, bool activeCaller) {
    std::unique_lock<std::mutex> worldLock(worldMutex, std::defer_lock);
    if (!activeCaller)
      worldLock.lock();
    ObjectMetadata *metadata = metadataFor(object);
    if (!metadata || metadata->heap != this)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t current = metadata->pins.load(std::memory_order_relaxed);
    do {
      if (current == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
    } while (!metadata->pins.compare_exchange_weak(current, current - 1,
                                                   std::memory_order_acq_rel));
    return OBELISK_RT_OK;
  }

  obelisk_rt_status allocate(obelisk_rt_gc_lane_v1 *lane,
                             const obelisk_rt_class_descriptor_v1 *descriptor,
                             obelisk_rt_object_v1 **outObject) noexcept {
    return guarded(context,
                   [&] { return allocateImpl(lane, descriptor, outObject); });
  }

private:
  obelisk_rt_status
  allocateImpl(obelisk_rt_gc_lane_v1 *lane,
               const obelisk_rt_class_descriptor_v1 *descriptor,
               obelisk_rt_object_v1 **outObject) {
    if (!outObject)
      return OBELISK_RT_INVALID_ARGUMENT;
    *outObject = nullptr;
    if (!activeOwner(lane))
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t currentEpoch = allocatorEpoch.load(std::memory_order_acquire);
    ThreadAllocationCache &validationCache =
        allocationCache(this, id, currentEpoch);
    if (std::find(validationCache.validatedDescriptors.begin(),
                  validationCache.validatedDescriptors.end(),
                  descriptor) == validationCache.validatedDescriptors.end()) {
      if (obelisk_rt_v1_class_validate(descriptor) != OBELISK_RT_OK)
        return OBELISK_RT_INVALID_ARGUMENT;
      validationCache
          .validatedDescriptors[validationCache.nextValidatedDescriptor] =
          descriptor;
      validationCache.nextValidatedDescriptor =
          (validationCache.nextValidatedDescriptor + 1) %
          validationCache.validatedDescriptors.size();
    }
    if ((descriptor->flags &
         (OBELISK_RT_CLASS_ABSTRACT | OBELISK_RT_CLASS_INTERFACE)) != 0)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (descriptor->instance_size >
        std::numeric_limits<uint64_t>::max() - sizeof(SlotPrefix) - 15)
      return OBELISK_RT_OUT_OF_RESOURCES;

    obelisk_rt_status status = safepoint(lane);
    if (status != OBELISK_RT_OK)
      return status;
    if (allocatedSinceCollection.load(std::memory_order_relaxed) >=
        nextCollectionBytes.load(std::memory_order_relaxed)) {
      status = collect(lane);
      if (status != OBELISK_RT_OK)
        return status;
    }

    if (descriptor->instance_size >
        std::numeric_limits<uint64_t>::max() - sizeof(SlotPrefix) - 15)
      return OBELISK_RT_OUT_OF_RESOURCES;
    uint64_t allocationSize =
        roundUp(sizeof(SlotPrefix) + descriptor->instance_size, 16);
    std::optional<uint64_t> objectIdentity = acquireObjectIdentity();
    if (!objectIdentity)
      return OBELISK_RT_OUT_OF_RESOURCES;
    // A safepoint above may have participated in a collection, invalidating
    // every cached span. Reacquire the TLS cache at the post-safepoint epoch
    // before dereferencing a span pointer.
    currentEpoch = allocatorEpoch.load(std::memory_order_acquire);
    ThreadAllocationCache &cache = allocationCache(this, id, currentEpoch);
    ObjectMetadata *metadata = nullptr;
    uint8_t *slotMemory = nullptr;
    if (allocationSize <= kMaximumSmallSlot) {
      uint32_t classSize = 0;
      unsigned classIndex = sizeClassFor(allocationSize, classSize);
      Span *span = cache.spans[classIndex];
      if (!span || !span->hasSpace()) {
        span = acquireSpan(classIndex, classSize);
        cache.spans[classIndex] = span;
      }

      uint32_t slot = 0;
      if (span->freeHead != UINT32_MAX) {
        slot = span->freeHead;
        uint8_t *freeMemory = span->memory + uint64_t(slot) * span->slotSize;
        std::memcpy(&span->freeHead, freeMemory, sizeof(span->freeHead));
      } else {
        slot = span->bump++;
      }
      slotMemory = span->memory + uint64_t(slot) * span->slotSize;
      metadata = &span->metadata[slot];
      metadata->heap = this;
      ++span->live;
    } else {
      auto large = std::make_unique<LargeAllocation>(allocationSize);
      slotMemory = large->storage;
      metadata = &large->metadata;
      metadata->heap = this;
      {
        std::lock_guard<std::mutex> lock(allocatorMutex);
        largeAllocations.push_back(std::move(large));
      }
      largeAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }

    std::memset(slotMemory, 0, allocationSize);
    auto *prefix = reinterpret_cast<SlotPrefix *>(slotMemory);
    auto *object = reinterpret_cast<obelisk_rt_object_v1 *>(slotMemory +
                                                            sizeof(SlotPrefix));
    prefix->metadata = metadata;
    prefix->magic = kObjectMagic;
    metadata->object = object;
    metadata->descriptor = descriptor;
    metadata->identity = *objectIdentity;
    metadata->pins.store(0, std::memory_order_relaxed);
    metadata->nextTicket.store(0, std::memory_order_relaxed);
    metadata->servingTicket.store(0, std::memory_order_relaxed);
    metadata->marked = false;
    std::memcpy(object, &descriptor, sizeof(descriptor));
    metadata->allocated.store(true, std::memory_order_release);

    allocatedObjects.fetch_add(1, std::memory_order_relaxed);
    allocatedSinceCollection.fetch_add(allocationSize,
                                       std::memory_order_relaxed);
    liveObjects.fetch_add(1, std::memory_order_relaxed);
    liveBytes.fetch_add(allocationSize, std::memory_order_relaxed);
    *outObject = object;
    return OBELISK_RT_OK;
  }

public:
  obelisk_rt_status collect(obelisk_rt_gc_lane_v1 *requester) {
    if (!activeOwner(requester))
      return OBELISK_RT_INVALID_LIFECYCLE;

    // An active lane must not block behind another collector: the current
    // collector is waiting for every active lane to park. Poll the safepoint
    // protocol while contending for collector ownership instead.
    while (!collectorMutex.try_lock()) {
      obelisk_rt_status status = safepoint(requester);
      if (status != OBELISK_RT_OK)
        return status;
      std::this_thread::yield();
    }
    std::unique_lock<std::mutex> collectorLock(collectorMutex, std::adopt_lock);
    std::unique_lock<std::mutex> worldLock(worldMutex);
    collectionRequested.store(true, std::memory_order_release);
    requester->state.store(LaneState::Collecting, std::memory_order_release);
    worldCondition.notify_all();
    worldCondition.wait(worldLock, [&] {
      for (obelisk_rt_gc_lane_v1 *lane : lanes) {
        if (lane == requester)
          continue;
        LaneState state = lane->state.load(std::memory_order_acquire);
        if (state == LaneState::Active || state == LaneState::Collecting)
          return false;
      }
      return true;
    });

    obelisk_rt_status status = OBELISK_RT_OK;
    try {
      std::lock_guard<std::mutex> allocatorLock(allocatorMutex);
      markAndSweep();
    } catch (const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
      std::lock_guard<std::mutex> allocatorLock(allocatorMutex);
      clearMarks();
    } catch (...) {
      status = OBELISK_RT_IO_ERROR;
      std::lock_guard<std::mutex> allocatorLock(allocatorMutex);
      clearMarks();
    }

    allocatorEpoch.fetch_add(1, std::memory_order_acq_rel);
    collectionRequested.store(false, std::memory_order_release);
    requester->state.store(LaneState::Active, std::memory_order_release);
    worldLock.unlock();
    worldCondition.notify_all();
    return status;
  }

  obelisk_rt_status setThreshold(uint64_t bytes) {
    if (bytes == 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    configuredThreshold.store(bytes, std::memory_order_relaxed);
    nextCollectionBytes.store(bytes, std::memory_order_relaxed);
    return OBELISK_RT_OK;
  }

  void statistics(obelisk_rt_gc_statistics_v1 &statistics) {
    statistics.live_objects = liveObjects.load(std::memory_order_relaxed);
    statistics.live_bytes = liveBytes.load(std::memory_order_relaxed);
    statistics.allocated_objects =
        allocatedObjects.load(std::memory_order_relaxed);
    statistics.reclaimed_objects =
        reclaimedObjects.load(std::memory_order_relaxed);
    statistics.collection_count =
        collectionCount.load(std::memory_order_relaxed);
    statistics.chunk_allocation_count =
        chunkAllocationCount.load(std::memory_order_relaxed);
    statistics.large_allocation_count =
        largeAllocationCount.load(std::memory_order_relaxed);
    statistics.cached_empty_chunks =
        cachedEmptyChunks.load(std::memory_order_relaxed);
    statistics.next_collection_bytes =
        nextCollectionBytes.load(std::memory_order_relaxed);
  }

  bool owns(obelisk_rt_gc_lane_v1 *lane) const {
    return lane && lane->heap == this;
  }

  bool activeOwner(obelisk_rt_gc_lane_v1 *lane) const {
    return owns(lane) &&
           lane->owner.load(std::memory_order_acquire) ==
               currentLaneOwnerToken() &&
           lane->state.load(std::memory_order_acquire) == LaneState::Active;
  }

  bool hasActiveCaller() {
    std::lock_guard<std::mutex> laneLock(laneMutex);
    return std::any_of(lanes.begin(), lanes.end(),
                       [&](obelisk_rt_gc_lane_v1 *lane) {
      return lane->owner.load(std::memory_order_acquire) ==
                 currentLaneOwnerToken() &&
             lane->state.load(std::memory_order_acquire) == LaneState::Active;
    });
  }

  obelisk_rt_status retainScheduled(obelisk_rt_gc_lane_v1 *lane,
                                    obelisk_rt_object_v1 *object) {
    // The active lane keeps the object alive until this increment is
    // published: a collector cannot pass that lane before its next safepoint.
    // This avoids taking worldMutex while the scheduler transaction lock is
    // held and therefore preserves the global GC/context lock order.
    if (!activeOwner(lane))
      return OBELISK_RT_INVALID_LIFECYCLE;
    ObjectMetadata *metadata = metadataFor(object);
    if (!metadata || metadata->heap != this)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t current = metadata->pins.load(std::memory_order_relaxed);
    do {
      if (current == UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
    } while (!metadata->pins.compare_exchange_weak(current, current + 1,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));
    return OBELISK_RT_OK;
  }

  obelisk_rt_status releaseScheduled(obelisk_rt_object_v1 *object) {
    ObjectMetadata *metadata = metadataFor(object);
    if (!metadata || metadata->heap != this)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t current = metadata->pins.load(std::memory_order_relaxed);
    do {
      if (current == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
    } while (!metadata->pins.compare_exchange_weak(current, current - 1,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));
    return OBELISK_RT_OK;
  }

  uint64_t identity() const { return id; }

private:
  std::optional<uint64_t> acquireObjectIdentity() {
    return acquireMonotonicIdentity(nextObjectID);
  }

  Span *acquireSpan(unsigned classIndex, uint32_t classSize) {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    auto &available = availableSpans[classIndex];
    while (!available.empty()) {
      Span *span = available.back();
      available.pop_back();
      if (span->hasSpace())
        return span;
    }

    Span *unassigned = nullptr;
    for (const auto &chunk : chunks) {
      for (Span &span : chunk->spans) {
        if (!span.assigned()) {
          unassigned = &span;
          break;
        }
      }
      if (unassigned)
        break;
    }
    if (!unassigned) {
      auto chunk = std::make_unique<Chunk>();
      unassigned = &chunk->spans.front();
      chunks.push_back(std::move(chunk));
      chunkAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }

    uint32_t capacity = static_cast<uint32_t>(kSpanSize / classSize);
    auto metadata = std::make_unique<ObjectMetadata[]>(capacity);
    unassigned->classIndex = classIndex;
    unassigned->slotSize = classSize;
    unassigned->capacity = capacity;
    unassigned->bump = 0;
    unassigned->freeHead = UINT32_MAX;
    unassigned->live = 0;
    unassigned->metadata = std::move(metadata);
    return unassigned;
  }

  void clearMarks() noexcept {
    for (const auto &chunk : chunks)
      for (Span &span : chunk->spans)
        for (uint32_t slot = 0; slot != span.bump; ++slot)
          span.metadata[slot].marked = false;
    for (const auto &large : largeAllocations)
      large->metadata.marked = false;
  }

  void mark(ObjectMetadata *metadata, std::vector<ObjectMetadata *> &pending) {
    if (!metadata || metadata->heap != this ||
        !metadata->allocated.load(std::memory_order_acquire) ||
        metadata->marked)
      return;
    metadata->marked = true;
    pending.push_back(metadata);
  }

  void markObject(obelisk_rt_object_v1 *object,
                  std::vector<ObjectMetadata *> &pending) {
    mark(metadataFor(object), pending);
  }

  struct WeakSlot {
    ObjectMetadata *owner;
    obelisk_rt_object_v1 **slot;
  };

  void traceLayout(uint8_t *base, const obelisk_rt_trace_layout_v1 *layout,
                   ObjectMetadata *owner,
                   std::vector<ObjectMetadata *> &pending,
                   std::vector<WeakSlot> &weakSlots) {
    if (!layout)
      return;
    for (uint64_t index = 0; index != layout->entry_count; ++index) {
      const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
      for (uint64_t item = 0; item != entry.count; ++item) {
        uint8_t *address = base + entry.offset + item * entry.stride;
        if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
          traceLayout(address, entry.child_layout, owner, pending, weakSlots);
          continue;
        }
        auto **slot = reinterpret_cast<obelisk_rt_object_v1 **>(address);
        if (entry.kind == OBELISK_RT_TRACE_WEAK)
          weakSlots.push_back({owner, slot});
        else
          markObject(*slot, pending);
      }
    }
  }

  void enumerateAllocated(const std::function<void(ObjectMetadata *)> &visit) {
    for (const auto &chunk : chunks)
      for (Span &span : chunk->spans)
        for (uint32_t slot = 0; slot != span.bump; ++slot)
          if (span.metadata[slot].allocated.load(std::memory_order_acquire))
            visit(&span.metadata[slot]);
    for (const auto &large : largeAllocations)
      if (large->metadata.allocated.load(std::memory_order_acquire))
        visit(&large->metadata);
  }

  void markAndSweep() {
    std::vector<ObjectMetadata *> pending;
    std::vector<WeakSlot> weakSlots;
    pending.reserve(
        static_cast<size_t>(liveObjects.load(std::memory_order_relaxed)));
    std::array<size_t, kSizeClassCount> assignedSpans{};
    for (const auto &chunk : chunks)
      for (const Span &span : chunk->spans)
        if (span.assigned())
          ++assignedSpans[span.classIndex];
    for (size_t index = 0; index != availableSpans.size(); ++index)
      availableSpans[index].reserve(assignedSpans[index]);

    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      for (obelisk_rt_gc_root_v1 *root =
               lane->roots.load(std::memory_order_acquire);
           root; root = root->previous)
        markObject(root->slot ? *root->slot : nullptr, pending);
    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      for (obelisk_rt_gc_root_range_v1 *range =
               lane->rootRanges.load(std::memory_order_acquire);
           range; range = range->previous)
        for (uint64_t index = 0; index != range->count; ++index)
          markObject(range->slots ? range->slots[index] : nullptr, pending);
    struct ProviderVisitor {
      ManagedHeap *heap;
      std::vector<ObjectMetadata *> *pending;
    } providerVisitor{this, &pending};
    auto visitProviderRoot = [](void *environment,
                                obelisk_rt_object_v1 **slot) {
      auto *visitor = static_cast<ProviderVisitor *>(environment);
      visitor->heap->markObject(slot ? *slot : nullptr, *visitor->pending);
    };
    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      for (ManagedRootProvider *provider =
               lane->providers.load(std::memory_order_acquire);
           provider; provider = provider->previous)
        if (provider->enumerate)
          provider->enumerate(provider->environment, visitProviderRoot,
                              &providerVisitor);
    {
      std::lock_guard<std::mutex> rootLock(rootMutex);
      for (obelisk_rt_object_v1 **slot : staticRoots)
        markObject(slot ? *slot : nullptr, pending);
    }
    obelisk_rt_enumerate_design_managed_roots(context, visitProviderRoot,
                                              &providerVisitor);
    enumerateAllocated([&](ObjectMetadata *metadata) {
      if (metadata->pins.load(std::memory_order_acquire) != 0)
        mark(metadata, pending);
    });

    while (!pending.empty()) {
      ObjectMetadata *metadata = pending.back();
      pending.pop_back();
      ObjectLock lock(metadata);
      traceLayout(static_cast<uint8_t *>(metadata->object),
                  metadata->descriptor->layout, metadata, pending, weakSlots);
    }

    for (const WeakSlot &weak : weakSlots) {
      ObjectLock lock(weak.owner);
      ObjectMetadata *referent = metadataFor(*weak.slot);
      if (referent && referent->heap == this && !referent->marked)
        *weak.slot = nullptr;
    }

    uint64_t currentLiveObjects = 0;
    uint64_t currentLiveBytes = 0;
    uint64_t reclaimed = 0;
    for (auto &pool : availableSpans)
      pool.clear();

    for (const auto &chunk : chunks) {
      for (Span &span : chunk->spans) {
        if (!span.assigned())
          continue;
        for (uint32_t slot = 0; slot != span.bump; ++slot) {
          ObjectMetadata &metadata = span.metadata[slot];
          if (!metadata.allocated.load(std::memory_order_acquire))
            continue;
          if (metadata.marked) {
            metadata.marked = false;
            ++currentLiveObjects;
            currentLiveBytes += roundUp(
                sizeof(SlotPrefix) + metadata.descriptor->instance_size, 16);
            continue;
          }
          metadata.allocated.store(false, std::memory_order_release);
          auto *prefix = reinterpret_cast<SlotPrefix *>(
              span.memory + uint64_t(slot) * span.slotSize);
          prefix->magic = 0;
          uint32_t previous = span.freeHead;
          std::memcpy(prefix, &previous, sizeof(previous));
          span.freeHead = slot;
          --span.live;
          ++reclaimed;
        }
        if (span.hasSpace())
          availableSpans[span.classIndex].push_back(&span);
      }
    }

    for (auto it = largeAllocations.begin(); it != largeAllocations.end();) {
      ObjectMetadata &metadata = (*it)->metadata;
      if (metadata.marked) {
        metadata.marked = false;
        ++currentLiveObjects;
        currentLiveBytes += roundUp(
            sizeof(SlotPrefix) + metadata.descriptor->instance_size, 16);
        ++it;
        continue;
      }
      metadata.allocated.store(false, std::memory_order_release);
      ++reclaimed;
      it = largeAllocations.erase(it);
    }

    // Keep two completely empty chunks hot. Removing excess chunks also
    // removes their spans from the just-built availability pools, so rebuild
    // the pools after the compacting erase. Reset kept empty chunks to
    // unassigned spans so churn in one size class cannot strand the cache when
    // a later phase allocates a different object size.
    size_t keptEmpty = 0;
    for (auto it = chunks.begin(); it != chunks.end();) {
      if (!(*it)->empty() || keptEmpty++ < kEmptyChunkCache) {
        ++it;
        continue;
      }
      it = chunks.erase(it);
    }
    for (auto &pool : availableSpans)
      pool.clear();
    size_t emptyChunks = 0;
    for (const auto &chunk : chunks) {
      bool empty = chunk->empty();
      emptyChunks += empty ? 1 : 0;
      if (empty)
        for (Span &span : chunk->spans) {
          span.classIndex = UINT32_MAX;
          span.slotSize = 0;
          span.capacity = 0;
          span.bump = 0;
          span.freeHead = UINT32_MAX;
          span.metadata.reset();
        }
      for (Span &span : chunk->spans)
        if (span.assigned() && span.hasSpace())
          availableSpans[span.classIndex].push_back(&span);
    }

    liveObjects.store(currentLiveObjects, std::memory_order_relaxed);
    liveBytes.store(currentLiveBytes, std::memory_order_relaxed);
    reclaimedObjects.fetch_add(reclaimed, std::memory_order_relaxed);
    collectionCount.fetch_add(1, std::memory_order_relaxed);
    cachedEmptyChunks.store(emptyChunks, std::memory_order_relaxed);
    allocatedSinceCollection.store(0, std::memory_order_relaxed);
    uint64_t configured = configuredThreshold.load(std::memory_order_relaxed);
    uint64_t dynamic =
        currentLiveBytes > std::numeric_limits<uint64_t>::max() / 2
            ? std::numeric_limits<uint64_t>::max()
            : currentLiveBytes * 2;
    nextCollectionBytes.store(
        configured ? configured : std::max(kMinimumCollectionBytes, dynamic),
        std::memory_order_relaxed);
  }

  obelisk_rt_context *context;
  const uint64_t id;
  std::mutex allocatorMutex;
  std::mutex collectorMutex;
  std::mutex worldMutex;
  std::mutex laneMutex;
  std::mutex rootMutex;
  std::condition_variable worldCondition;
  std::atomic<bool> collectionRequested{false};
  std::vector<obelisk_rt_gc_lane_v1 *> lanes;
  std::vector<obelisk_rt_object_v1 **> staticRoots;
  std::vector<std::unique_ptr<Chunk>> chunks;
  std::vector<std::unique_ptr<LargeAllocation>> largeAllocations;
  std::array<std::vector<Span *>, kSizeClassCount> availableSpans;
  std::atomic<uint64_t> allocatorEpoch{1};
  std::atomic<uint64_t> nextObjectID{1};
  std::atomic<uint64_t> allocatedSinceCollection{0};
  std::atomic<uint64_t> configuredThreshold{0};
  std::atomic<uint64_t> nextCollectionBytes{kMinimumCollectionBytes};
  std::atomic<uint64_t> liveObjects{0};
  std::atomic<uint64_t> liveBytes{0};
  std::atomic<uint64_t> allocatedObjects{0};
  std::atomic<uint64_t> reclaimedObjects{0};
  std::atomic<uint64_t> collectionCount{0};
  std::atomic<uint64_t> chunkAllocationCount{0};
  std::atomic<uint64_t> largeAllocationCount{0};
  std::atomic<uint64_t> cachedEmptyChunks{0};
};

ManagedHeap *obelisk_rt_managed_heap_create(obelisk_rt_context *context) {
  return new ManagedHeap(context);
}

void obelisk_rt_managed_heap_destroy(ManagedHeap *heap) noexcept {
  delete heap;
}

namespace {

struct ThreadExecutionLane {
  ManagedHeap *heap = nullptr;
  uint64_t heapID = 0;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
};

thread_local std::vector<ThreadExecutionLane> executionLanes;

ManagedHeap *heapFor(obelisk_rt_context *context) {
  return context ? context->managedHeap : nullptr;
}

obelisk_rt_status threadExecutionLane(ManagedHeap *heap,
                                      obelisk_rt_gc_lane_v1 **outLane) {
  if (!heap || !outLane)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outLane = nullptr;
  for (ThreadExecutionLane &entry : executionLanes) {
    // The id prevents a stale TLS entry from matching if an allocator reuses
    // the address of a context that was destroyed on another thread.
    if (entry.heap == heap && entry.heapID == heap->identity()) {
      *outLane = entry.lane;
      return OBELISK_RT_OK;
    }
  }

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_status status = heap->createLane(&lane);
  if (status != OBELISK_RT_OK)
    return status;
  try {
    executionLanes.push_back({heap, heap->identity(), lane});
  } catch (...) {
    (void)heap->destroyLane(lane);
    throw;
  }
  *outLane = lane;
  return OBELISK_RT_OK;
}

const obelisk_rt_class_descriptor_v1 *
descriptorFor(const obelisk_rt_object_v1 *object) {
  ObjectMetadata *metadata = metadataFor(object);
  return metadata ? metadata->descriptor : nullptr;
}

} // namespace

obelisk_rt_status
obelisk_rt_managed_execution_enter(obelisk_rt_context *context,
                                   obelisk_rt_gc_lane_v1 **outLane,
                                   bool *outEntered) {
  if (!outLane || !outEntered)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outLane = nullptr;
  *outEntered = false;
  ManagedHeap *heap = heapFor(context);
  if (!heap)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    obelisk_rt_gc_lane_v1 *lane = nullptr;
    obelisk_rt_status status = threadExecutionLane(heap, &lane);
    if (status != OBELISK_RT_OK)
      return status;
    if (heap->activeOwner(lane)) {
      *outLane = lane;
      return OBELISK_RT_OK;
    }
    status = heap->enter(lane);
    if (status != OBELISK_RT_OK)
      return status;
    *outLane = lane;
    *outEntered = true;
    return OBELISK_RT_OK;
  });
}

void obelisk_rt_managed_execution_leave(obelisk_rt_gc_lane_v1 *lane,
                                        bool entered) {
  if (entered && lane && lane->heap)
    (void)lane->heap->leave(lane);
}

obelisk_rt_status obelisk_rt_managed_roots_push(obelisk_rt_gc_lane_v1 *lane,
                                                ManagedRootProvider *provider,
                                                ManagedRootEnumerate enumerate,
                                                void *environment) {
  return lane && lane->heap
             ? lane->heap->pushProvider(lane, provider, enumerate, environment)
             : OBELISK_RT_INVALID_ARGUMENT;
}

obelisk_rt_status obelisk_rt_managed_roots_pop(obelisk_rt_gc_lane_v1 *lane,
                                               ManagedRootProvider *provider) {
  return lane && lane->heap ? lane->heap->popProvider(lane, provider)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

const obelisk_rt_class_descriptor_v1 *
obelisk_rt_managed_class_lookup(obelisk_rt_context *context, uint64_t classID) {
  if (!context || classID == 0)
    return nullptr;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->managedClasses.find(classID);
  return found == context->managedClasses.end() ? nullptr : found->second;
}

bool obelisk_rt_managed_object_belongs_to(
    obelisk_rt_context *context, obelisk_rt_object_v1 *object) noexcept {
  if (!object)
    return true;
  ManagedHeap *heap = heapFor(context);
  ObjectMetadata *metadata = metadataFor(object);
  return heap && metadata && metadata->heap == heap;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_class_validate(const obelisk_rt_class_descriptor_v1 *descriptor) {
  constexpr uint32_t validFlags =
      OBELISK_RT_CLASS_ABSTRACT | OBELISK_RT_CLASS_INTERFACE |
      OBELISK_RT_CLASS_FINAL | OBELISK_RT_CLASS_WEAK_WRAPPER;
  constexpr uint32_t validMethodFlags =
      OBELISK_RT_METHOD_TASK | OBELISK_RT_METHOD_PURE;
  if (!descriptor)
    return OBELISK_RT_INVALID_DESIGN;

  // Validate cycles before the main walk so all subsequent pairwise checks
  // are allocation-free and guaranteed to terminate.
  const obelisk_rt_class_descriptor_v1 *slow = descriptor;
  const obelisk_rt_class_descriptor_v1 *fast = descriptor;
  while (fast && fast->base) {
    slow = slow->base;
    fast = fast->base->base;
    if (slow == fast)
      return OBELISK_RT_INVALID_DESIGN;
  }

  const obelisk_rt_class_descriptor_v1 *derived = nullptr;
  for (const obelisk_rt_class_descriptor_v1 *current = descriptor; current;
       current = current->base) {
    for (const obelisk_rt_class_descriptor_v1 *previous = descriptor;
         previous != current; previous = previous->base)
      if (previous->class_id == current->class_id)
        return OBELISK_RT_INVALID_DESIGN;
    if (current->version != OBELISK_RT_VERSION || current->class_id == 0 ||
        current->instance_size < sizeof(current) ||
        !validPowerOfTwo(current->instance_alignment) ||
        current->instance_alignment > 16 ||
        (current->flags & ~validFlags) != 0 ||
        (current->interface_count == 0) !=
            (current->interface_ids == nullptr) ||
        (current->method_count == 0) != (current->methods == nullptr) ||
        (current->debug_name_size == 0) != (current->debug_name == nullptr) ||
        (derived &&
         ((current->flags & OBELISK_RT_CLASS_FINAL) != 0 ||
          derived->instance_size < current->instance_size ||
          derived->instance_alignment < current->instance_alignment ||
          derived->method_count < current->method_count)))
      return OBELISK_RT_INVALID_DESIGN;
    for (uint64_t index = 0; index != current->interface_count; ++index) {
      if (current->interface_ids[index] == 0) {
        return OBELISK_RT_INVALID_DESIGN;
      }
      for (uint64_t previous = 0; previous != index; ++previous)
        if (current->interface_ids[previous] == current->interface_ids[index])
          return OBELISK_RT_INVALID_DESIGN;
    }
    for (uint64_t index = 0; index != current->method_count; ++index) {
      const obelisk_rt_method_descriptor_v1 &method = current->methods[index];
      if (method.signature_id == 0 || (method.flags & ~validMethodFlags) != 0 ||
          (!method.native_entry &&
           method.bytecode_function == OBELISK_RT_METHOD_NO_BYTECODE &&
           (method.flags & OBELISK_RT_METHOD_PURE) == 0))
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (derived)
      for (uint64_t index = 0; index != current->method_count; ++index) {
        const obelisk_rt_method_descriptor_v1 &baseMethod =
            current->methods[index];
        const obelisk_rt_method_descriptor_v1 &overrideMethod =
            derived->methods[index];
        if (overrideMethod.signature_id != baseMethod.signature_id ||
            ((overrideMethod.flags ^ baseMethod.flags) &
             OBELISK_RT_METHOD_TASK) != 0)
          return OBELISK_RT_INVALID_DESIGN;
      }
    if (current->layout) {
      if (current->layout->size != current->instance_size ||
          !validateTraceLayout(current->layout, current->instance_size) ||
          !layoutHandlesAreUnique(current->layout, current->layout) ||
          layoutOverlapsHandle(current->layout, 0, 0, sizeof(void *)))
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (derived && !layoutContainsHandles(derived->layout, current->layout))
      return OBELISK_RT_INVALID_DESIGN;
    if ((current->flags & OBELISK_RT_CLASS_WEAK_WRAPPER) != 0 &&
        (!current->layout ||
         !layoutHasWeakHandleAt(current->layout, 0, sizeof(void *)) ||
         layoutHasStrongHandleAt(current->layout, 0, sizeof(void *))))
      return OBELISK_RT_INVALID_DESIGN;
    derived = current;
  }
  if ((descriptor->flags &
       (OBELISK_RT_CLASS_ABSTRACT | OBELISK_RT_CLASS_INTERFACE)) == 0)
    for (uint64_t index = 0; index != descriptor->method_count; ++index)
      if ((descriptor->methods[index].flags & OBELISK_RT_METHOD_PURE) != 0)
        return OBELISK_RT_INVALID_DESIGN;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_lane_create(obelisk_rt_context *context,
                             obelisk_rt_gc_lane_v1 **outLane) {
  ManagedHeap *heap = heapFor(context);
  if (!heap)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] { return heap->createLane(outLane); });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_lane_destroy(obelisk_rt_gc_lane_v1 *lane) {
  return lane && lane->heap ? lane->heap->destroyLane(lane)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_lane_enter(obelisk_rt_gc_lane_v1 *lane) {
  return lane && lane->heap ? lane->heap->enter(lane)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_lane_leave(obelisk_rt_gc_lane_v1 *lane) {
  return lane && lane->heap ? lane->heap->leave(lane)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_safepoint(obelisk_rt_gc_lane_v1 *lane) {
  return lane && lane->heap ? lane->heap->safepoint(lane)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_gc_lane_v1 *
obelisk_rt_v1_gc_current_lane(obelisk_rt_context *context) {
  ManagedHeap *heap = heapFor(context);
  if (!heap)
    return nullptr;
  for (ThreadExecutionLane &entry : executionLanes) {
    if (entry.heap == heap && entry.heapID == heap->identity() &&
        heap->activeOwner(entry.lane))
      return entry.lane;
  }
  return nullptr;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_root_push(obelisk_rt_gc_lane_v1 *lane,
                           obelisk_rt_gc_root_v1 *root,
                           obelisk_rt_object_v1 **slot) {
  return lane && lane->heap ? lane->heap->pushRoot(lane, root, slot)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_root_pop(obelisk_rt_gc_lane_v1 *lane,
                          obelisk_rt_gc_root_v1 *root) {
  return lane && lane->heap ? lane->heap->popRoot(lane, root)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_root_range_push(obelisk_rt_gc_lane_v1 *lane,
                                 obelisk_rt_gc_root_range_v1 *range,
                                 obelisk_rt_object_v1 **slots, uint64_t count) {
  return lane && lane->heap
             ? lane->heap->pushRootRange(lane, range, slots, count)
             : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_root_range_pop(obelisk_rt_gc_lane_v1 *lane,
                                obelisk_rt_gc_root_range_v1 *range) {
  return lane && lane->heap ? lane->heap->popRootRange(lane, range)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_static_root_register(obelisk_rt_context *context,
                                      obelisk_rt_object_v1 **slot) {
  ManagedHeap *heap = heapFor(context);
  bool activeCaller = heap && heap->hasActiveCaller();
  return heap
             ? guarded(context,
                       [&] { return heap->registerStatic(slot, activeCaller); })
             : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_static_root_unregister(obelisk_rt_context *context,
                                        obelisk_rt_object_v1 **slot) {
  ManagedHeap *heap = heapFor(context);
  bool activeCaller = heap && heap->hasActiveCaller();
  return heap ? heap->unregisterStatic(slot, activeCaller)
              : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_pin(obelisk_rt_context *context,
                     obelisk_rt_object_v1 *object) {
  ManagedHeap *heap = heapFor(context);
  bool activeCaller = heap && heap->hasActiveCaller();
  return heap ? heap->pin(object, activeCaller) : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_unpin(obelisk_rt_context *context,
                       obelisk_rt_object_v1 *object) {
  ManagedHeap *heap = heapFor(context);
  bool activeCaller = heap && heap->hasActiveCaller();
  return heap ? heap->unpin(object, activeCaller) : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_collect(obelisk_rt_gc_lane_v1 *lane) {
  return lane && lane->heap ? lane->heap->collect(lane)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_set_threshold(obelisk_rt_context *context,
                               uint64_t allocationBytes) {
  ManagedHeap *heap = heapFor(context);
  return heap ? heap->setThreshold(allocationBytes)
              : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_statistics(obelisk_rt_context *context,
                            obelisk_rt_gc_statistics_v1 *outStatistics) {
  ManagedHeap *heap = heapFor(context);
  if (!heap || !outStatistics)
    return OBELISK_RT_INVALID_ARGUMENT;
  heap->statistics(*outStatistics);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_class_register(obelisk_rt_context *context,
                             const obelisk_rt_class_descriptor_v1 *descriptor) {
  if (!context || obelisk_rt_v1_class_validate(descriptor) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto [found, inserted] =
        context->managedClasses.try_emplace(descriptor->class_id, descriptor);
    return inserted || found->second == descriptor ? OBELISK_RT_OK
                                                   : OBELISK_RT_INVALID_DESIGN;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_design_root_register(obelisk_rt_context *context,
                                      uint64_t bitOffset) {
  ManagedHeap *heap = heapFor(context);
  if (!heap || bitOffset % 64 != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t word = bitOffset / 64;
  if (word >= context->stateValue.size())
    return OBELISK_RT_INVALID_DESIGN;
  return guarded(context, [&] {
    auto **slot = reinterpret_cast<obelisk_rt_object_v1 **>(
        context->stateValue.data() + word);
    bool activeCaller = heap->hasActiveCaller();
    return heap->registerStatic(slot, activeCaller);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_allocate(obelisk_rt_gc_lane_v1 *lane,
                              const obelisk_rt_class_descriptor_v1 *descriptor,
                              obelisk_rt_object_v1 **outObject) {
  return lane && lane->heap ? lane->heap->allocate(lane, descriptor, outObject)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status obelisk_rt_v1_object_shallow_copy(
    obelisk_rt_gc_lane_v1 *lane,
    const obelisk_rt_class_descriptor_v1 *staticDescriptor,
    obelisk_rt_object_v1 *source, obelisk_rt_object_v1 **outObject) {
  if (!outObject)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outObject = nullptr;
  if (!lane || !lane->heap || !source ||
      !obelisk_rt_v1_object_is_instance(source, staticDescriptor))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *sourceMetadata = metadataFor(source);
  if (!sourceMetadata || sourceMetadata->heap != lane->heap)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_gc_root_v1 sourceRoot{};
  obelisk_rt_status status = lane->heap->pushRoot(lane, &sourceRoot, &source);
  if (status != OBELISK_RT_OK)
    return status;
  status = lane->heap->allocate(lane, staticDescriptor, outObject);
  if (status != OBELISK_RT_OK) {
    (void)lane->heap->popRoot(lane, &sourceRoot);
    return status;
  }
  ObjectLock lock(sourceMetadata);
  std::memcpy(reinterpret_cast<uint8_t *>(*outObject) + sizeof(void *),
              reinterpret_cast<const uint8_t *>(source) + sizeof(void *),
              staticDescriptor->instance_size - sizeof(void *));
  return lane->heap->popRoot(lane, &sourceRoot);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_read(obelisk_rt_object_v1 *object, uint64_t offset,
                          void *data, uint64_t size) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || (!data && size != 0) ||
      !checkedRange(offset, size, metadata->descriptor->instance_size))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectLock lock(metadata);
  std::memcpy(data, reinterpret_cast<uint8_t *>(object) + offset, size);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_write(obelisk_rt_object_v1 *object, uint64_t offset,
                           const void *data, uint64_t size) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || (!data && size != 0) || offset < sizeof(void *) ||
      !checkedRange(offset, size, metadata->descriptor->instance_size) ||
      !validateLayoutHandleWrite(metadata->descriptor->layout, 0, offset, size,
                                 static_cast<const uint8_t *>(data),
                                 metadata->heap))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectLock lock(metadata);
  std::memcpy(reinterpret_cast<uint8_t *>(object) + offset, data, size);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_read_planes(obelisk_rt_object_v1 *object, uint64_t offset,
                                 void *value, void *unknown,
                                 uint64_t planeSize) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || !value || !unknown || planeSize == 0 ||
      planeSize > UINT64_MAX / 2 ||
      !checkedRange(offset, planeSize * 2, metadata->descriptor->instance_size))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectLock lock(metadata);
  const uint8_t *source = reinterpret_cast<const uint8_t *>(object) + offset;
  std::memcpy(value, source, planeSize);
  std::memcpy(unknown, source + planeSize, planeSize);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_write_planes(obelisk_rt_object_v1 *object, uint64_t offset,
                                  const void *value, const void *unknown,
                                  uint64_t planeSize) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || !value || !unknown || planeSize == 0 ||
      offset < sizeof(void *) || planeSize > UINT64_MAX / 2 ||
      !checkedRange(offset, planeSize * 2,
                    metadata->descriptor->instance_size) ||
      !validateLayoutHandleWrite(metadata->descriptor->layout, 0, offset,
                                 planeSize, static_cast<const uint8_t *>(value),
                                 metadata->heap) ||
      !validateLayoutHandleWrite(
          metadata->descriptor->layout, 0, offset + planeSize, planeSize,
          static_cast<const uint8_t *>(unknown), metadata->heap))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectLock lock(metadata);
  uint8_t *destination = reinterpret_cast<uint8_t *>(object) + offset;
  std::memcpy(destination, value, planeSize);
  std::memcpy(destination + planeSize, unknown, planeSize);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_field_load(obelisk_rt_object_v1 *object, uint64_t offset,
                                obelisk_rt_object_v1 **outValue) {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outValue = nullptr;
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata ||
      !checkedRange(offset, sizeof(*outValue),
                    metadata->descriptor->instance_size) ||
      !layoutHasHandleAt(metadata->descriptor->layout, 0, offset))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectLock lock(metadata);
  std::memcpy(outValue, reinterpret_cast<uint8_t *>(object) + offset,
              sizeof(*outValue));
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_field_store(obelisk_rt_object_v1 *object, uint64_t offset,
                                 obelisk_rt_object_v1 *value) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata ||
      !checkedRange(offset, sizeof(value),
                    metadata->descriptor->instance_size) ||
      !layoutHasHandleAt(metadata->descriptor->layout, 0, offset))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (value) {
    ObjectMetadata *valueMetadata = metadataFor(value);
    if (!valueMetadata || valueMetadata->heap != metadata->heap)
      return OBELISK_RT_INVALID_HANDLE;
  }
  ObjectLock lock(metadata);
  std::memcpy(reinterpret_cast<uint8_t *>(object) + offset, &value,
              sizeof(value));
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_managed_nba(
    obelisk_rt_context *context, obelisk_rt_object_v1 *destination,
    uint64_t offset, const void *value, const void *unknown, uint64_t planeSize,
    uint64_t delay) {
  ManagedHeap *heap = heapFor(context);
  ObjectMetadata *destinationMetadata = metadataFor(destination);
  if (!heap || !destinationMetadata || destinationMetadata->heap != heap ||
      !value || planeSize == 0 ||
      planeSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;

  const obelisk_rt_trace_layout_v1 *layout =
      destinationMetadata->descriptor->layout;
  bool managedSlot = planeSize == sizeof(obelisk_rt_object_v1 *) &&
                     layoutHasHandleAt(layout, 0, offset);
  std::vector<obelisk_rt_object_v1 *> referents;
  if (managedSlot) {
    if (unknown || planeSize != sizeof(obelisk_rt_object_v1 *) ||
        !checkedRange(offset, planeSize,
                      destinationMetadata->descriptor->instance_size))
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_object_v1 *referent = nullptr;
    std::memcpy(&referent, value, sizeof(referent));
    if (referent) {
      ObjectMetadata *referentMetadata = metadataFor(referent);
      if (!referentMetadata || referentMetadata->heap != heap)
        return OBELISK_RT_INVALID_HANDLE;
      referents.push_back(referent);
    }
  } else {
    if (!checkedRange(offset, planeSize,
                      destinationMetadata->descriptor->instance_size) ||
        !validateLayoutHandleWrite(layout, 0, offset, planeSize,
                                   static_cast<const uint8_t *>(value), heap,
                                   &referents))
      return OBELISK_RT_INVALID_ARGUMENT;
    if (unknown &&
        (!checkedRange(offset + planeSize, planeSize,
                       destinationMetadata->descriptor->instance_size) ||
         !validateLayoutHandleWrite(layout, 0, offset + planeSize, planeSize,
                                    static_cast<const uint8_t *>(unknown),
                                    heap)))
      return OBELISK_RT_INVALID_ARGUMENT;
  }

  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  obelisk_rt_status status = heap->retainScheduled(lane, destination);
  if (status != OBELISK_RT_OK)
    return status;
  size_t retainedReferents = 0;
  for (obelisk_rt_object_v1 *referent : referents) {
    status = heap->retainScheduled(lane, referent);
    if (status != OBELISK_RT_OK) {
      while (retainedReferents != 0)
        (void)heap->releaseScheduled(referents[--retainedReferents]);
      (void)heap->releaseScheduled(destination);
      return status;
    }
    ++retainedReferents;
  }

  auto rollback = [&] {
    while (retainedReferents != 0)
      (void)heap->releaseScheduled(referents[--retainedReferents]);
    (void)heap->releaseScheduled(destination);
  };
  try {
    ScheduledManagedNBA update;
    update.destination = destination;
    update.offset = offset;
    update.planeSize = planeSize;
    update.value.assign(static_cast<const uint8_t *>(value),
                        static_cast<const uint8_t *>(value) +
                            static_cast<size_t>(planeSize));
    if (unknown)
      update.unknown.assign(static_cast<const uint8_t *>(unknown),
                            static_cast<const uint8_t *>(unknown) +
                                static_cast<size_t>(planeSize));
    // Keep `referents` intact until publication succeeds so exception
    // rollback can release every scheduled root.
    update.managedValues = referents;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->nextSchedulerSequence == 0) {
      rollback();
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    update.sequence = context->nextSchedulerSequence++;
    update.dueTime = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
    context->scheduledManagedNBAs.push_back(std::move(update));
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    rollback();
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    rollback();
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

obelisk_rt_status
obelisk_rt_apply_managed_nba(obelisk_rt_context *context,
                             const ScheduledManagedNBA &update) {
  ManagedHeap *heap = heapFor(context);
  if (!heap)
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *destination = metadataFor(update.destination);
  obelisk_rt_status status = OBELISK_RT_OK;
  if (!destination || destination->heap != heap ||
      update.value.size() != update.planeSize ||
      (!update.unknown.empty() && update.unknown.size() != update.planeSize)) {
    status = OBELISK_RT_INVALID_HANDLE;
  } else {
    ObjectLock lock(destination);
    uint8_t *base = reinterpret_cast<uint8_t *>(update.destination);
    std::memcpy(base + update.offset, update.value.data(),
                static_cast<size_t>(update.planeSize));
    if (!update.unknown.empty())
      std::memcpy(base + update.offset + update.planeSize,
                  update.unknown.data(), static_cast<size_t>(update.planeSize));
  }

  for (obelisk_rt_object_v1 *managedValue : update.managedValues) {
    obelisk_rt_status released = heap->releaseScheduled(managedValue);
    if (status == OBELISK_RT_OK && released != OBELISK_RT_OK)
      status = released;
  }
  obelisk_rt_status released = heap->releaseScheduled(update.destination);
  if (status == OBELISK_RT_OK && released != OBELISK_RT_OK)
    status = released;
  return status;
}

extern "C" uint32_t
obelisk_rt_v1_object_is_instance(const obelisk_rt_object_v1 *object,
                                 const obelisk_rt_class_descriptor_v1 *target) {
  if (!object || !target)
    return 0;
  const obelisk_rt_class_descriptor_v1 *descriptor = descriptorFor(object);
  for (const obelisk_rt_class_descriptor_v1 *current = descriptor; current;
       current = current->base) {
    if (current == target || current->class_id == target->class_id)
      return 1;
    for (uint64_t index = 0; index != current->interface_count; ++index)
      if (current->interface_ids[index] == target->class_id)
        return 1;
  }
  return 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_cast(obelisk_rt_object_v1 *object,
                          const obelisk_rt_class_descriptor_v1 *target,
                          obelisk_rt_object_v1 **outObject) {
  if (!target || !outObject)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!object) {
    *outObject = nullptr;
    return OBELISK_RT_OK;
  }
  if (!metadataFor(object)) {
    *outObject = nullptr;
    return OBELISK_RT_INVALID_HANDLE;
  }
  if (!obelisk_rt_v1_object_is_instance(object, target)) {
    *outObject = nullptr;
    return OBELISK_RT_OK;
  }
  *outObject = object;
  return OBELISK_RT_OK;
}

extern "C" uint64_t
obelisk_rt_v1_object_id(const obelisk_rt_object_v1 *object) {
  ObjectMetadata *metadata = metadataFor(object);
  return metadata ? metadata->identity : 0;
}

extern "C" obelisk_rt_status obelisk_rt_v1_method_resolve(
    obelisk_rt_object_v1 *receiver, uint64_t slot, uint64_t signatureID,
    const obelisk_rt_method_descriptor_v1 **outMethod) {
  const obelisk_rt_class_descriptor_v1 *descriptor = descriptorFor(receiver);
  if (!descriptor || !outMethod)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  if (slot == UINT32_MAX) {
    for (uint64_t index = 0; index != descriptor->method_count; ++index)
      if (descriptor->methods[index].signature_id == signatureID) {
        method = &descriptor->methods[index];
        break;
      }
  } else if (slot < descriptor->method_count) {
    method = &descriptor->methods[slot];
  }
  if (!method)
    return OBELISK_RT_INVALID_HANDLE;
  if (method->signature_id != signatureID)
    return OBELISK_RT_LAYOUT_MISMATCH;
  *outMethod = method;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_method_invoke(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signatureID, const obelisk_rt_method_argument_v1 *arguments,
    uint32_t argumentCount, void *result, uint64_t resultSize) {
  if (!lane || !lane->heap || !lane->heap->activeOwner(lane) ||
      (!arguments && argumentCount != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *metadata = metadataFor(receiver);
  if (!metadata || metadata->heap != lane->heap)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  obelisk_rt_status status =
      obelisk_rt_v1_method_resolve(receiver, slot, signatureID, &method);
  if (status != OBELISK_RT_OK)
    return status;
  if (!method->native_entry)
    return OBELISK_RT_TIER_UNAVAILABLE;
  // A foreign caller is not required to have placed the receiver in its own
  // root set. Keep it alive across allocations made by the method body.
  obelisk_rt_gc_root_v1 receiverRoot{};
  status = lane->heap->pushRoot(lane, &receiverRoot, &receiver);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_status callStatus = OBELISK_RT_OK;
  try {
    callStatus = method->native_entry(lane->context, lane, receiver, arguments,
                                      argumentCount, result, resultSize);
  } catch (const std::bad_alloc &) {
    callStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    callStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_status popStatus = lane->heap->popRoot(lane, &receiverRoot);
  return callStatus == OBELISK_RT_OK ? popStatus : callStatus;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_weak_create(obelisk_rt_gc_lane_v1 *lane,
                          const obelisk_rt_class_descriptor_v1 *descriptor,
                          obelisk_rt_object_v1 *referent,
                          obelisk_rt_object_v1 **outWeak) {
  if (!outWeak)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outWeak = nullptr;
  if (!lane || !lane->heap || !descriptor ||
      (descriptor->flags & OBELISK_RT_CLASS_WEAK_WRAPPER) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (referent) {
    ObjectMetadata *metadata = metadataFor(referent);
    if (!metadata || metadata->heap != lane->heap)
      return OBELISK_RT_INVALID_HANDLE;
  }
  obelisk_rt_gc_root_v1 referentRoot{};
  obelisk_rt_status status =
      lane->heap->pushRoot(lane, &referentRoot, &referent);
  if (status != OBELISK_RT_OK)
    return status;
  status = lane->heap->allocate(lane, descriptor, outWeak);
  if (status != OBELISK_RT_OK) {
    (void)lane->heap->popRoot(lane, &referentRoot);
    return status;
  }
  status = obelisk_rt_v1_object_field_store(
      *outWeak, sizeof(const obelisk_rt_class_descriptor_v1 *), referent);
  obelisk_rt_status popStatus = lane->heap->popRoot(lane, &referentRoot);
  return status == OBELISK_RT_OK ? popStatus : status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_weak_get(obelisk_rt_object_v1 *weak,
                       obelisk_rt_object_v1 **outReferent) {
  const obelisk_rt_class_descriptor_v1 *descriptor = descriptorFor(weak);
  if (!descriptor || (descriptor->flags & OBELISK_RT_CLASS_WEAK_WRAPPER) == 0)
    return OBELISK_RT_INVALID_HANDLE;
  return obelisk_rt_v1_object_field_load(
      weak, sizeof(const obelisk_rt_class_descriptor_v1 *), outReferent);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_weak_clear(obelisk_rt_object_v1 *weak) {
  const obelisk_rt_class_descriptor_v1 *descriptor = descriptorFor(weak);
  if (!descriptor || (descriptor->flags & OBELISK_RT_CLASS_WEAK_WRAPPER) == 0)
    return OBELISK_RT_INVALID_HANDLE;
  return obelisk_rt_v1_object_field_store(
      weak, sizeof(const obelisk_rt_class_descriptor_v1 *), nullptr);
}
