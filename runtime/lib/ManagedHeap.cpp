//===- ManagedHeap.cpp - Precise SystemVerilog class heap ----------------===//

#include "RuntimeInternal.h"

#include <array>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <shared_mutex>
#include <unordered_set>

struct obelisk_rt_object_v1 {};

struct obelisk_rt_random_graph_v1 {
  struct ObjectBinding {
    obelisk_rt_object_v1 *object = nullptr;
    const obelisk_rt_class_descriptor_v1 *descriptor = nullptr;
  };

  struct VariableBinding {
    obelisk_rt_object_v1 *object = nullptr;
    const obelisk_rt_random_variable_v1 *variable = nullptr;
  };

  obelisk_rt_context *context = nullptr;
  std::vector<ObjectBinding> objects;
  std::vector<VariableBinding> variables;
};

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
constexpr uint32_t kRandomVariableFlags =
    OBELISK_RT_RANDOM_VARIABLE_FOUR_STATE | OBELISK_RT_RANDOM_VARIABLE_SIGNED |
    OBELISK_RT_RANDOM_VARIABLE_RANDC;

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
  uint64_t extent = 0;
  uint64_t allocationSize = 0;
  obelisk_rt_managed_kind_v1 kind = OBELISK_RT_MANAGED_INVALID;
  uint32_t alignment = 0;
  std::atomic<uint32_t> pins{0};
  std::atomic<uint32_t> nextTicket{0};
  std::atomic<uint32_t> servingTicket{0};
  std::atomic<bool> allocated{false};
  bool marked = false;
};

static_assert(sizeof(ObjectMetadata) <= 96,
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
  uint64_t alignment = 16;
  ObjectMetadata metadata;

  explicit LargeAllocation(uint64_t allocationSize, uint64_t alignment)
      : alignment(std::max<uint64_t>(alignment, 16)) {
    storage = static_cast<uint8_t *>(
        ::operator new(allocationSize, std::align_val_t(this->alignment)));
    std::memset(storage, 0, allocationSize);
  }

  ~LargeAllocation() {
    ::operator delete(storage, std::align_val_t(alignment));
  }
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

std::shared_mutex metadataRegistryMutex;
std::unordered_map<const obelisk_rt_object_v1 *, ObjectMetadata *>
    metadataRegistry;

void registerMetadata(ObjectMetadata *metadata) {
  std::unique_lock<std::shared_mutex> lock(metadataRegistryMutex);
  metadataRegistry.emplace(
      static_cast<const obelisk_rt_object_v1 *>(metadata->object), metadata);
}

void unregisterMetadata(ObjectMetadata *metadata) {
  std::unique_lock<std::shared_mutex> lock(metadataRegistryMutex);
  metadataRegistry.erase(
      static_cast<const obelisk_rt_object_v1 *>(metadata->object));
}

ObjectMetadata *metadataFor(const obelisk_rt_object_v1 *object) {
  if (!object)
    return nullptr;
  std::shared_lock<std::shared_mutex> lock(metadataRegistryMutex);
  auto found = metadataRegistry.find(object);
  if (found == metadataRegistry.end())
    return nullptr;
  ObjectMetadata *metadata = found->second;
  if (metadata->object != object ||
      !metadata->allocated.load(std::memory_order_acquire))
    return nullptr;
  return metadata;
}

obelisk_rt_object_v1 *
managedWordObject(obelisk_rt_managed_word_v1 word) noexcept {
  if (word == 0 || (word & UINT64_C(3)) != 0)
    return nullptr;
  return reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word));
}

bool validImmediateManagedWord(obelisk_rt_managed_word_v1 word) noexcept {
  if ((word & UINT64_C(3)) != UINT64_C(1))
    return false;
  uint8_t control = static_cast<uint8_t>(word);
  uint64_t length = (word >> 2) & 7;
  if ((control & UINT8_C(0xe0)) != 0 || length == 0)
    return false;
  uint64_t usedBits = 8 + length * 8;
  return usedBits == 64 || (word >> usedBits) == 0;
}

bool isCandidateSlotKind(obelisk_rt_managed_slot_kind_v1 kind) noexcept {
  return (kind & OBELISK_RT_MANAGED_SLOT_CANDIDATE) != 0;
}

uint32_t candidateSlotMask(obelisk_rt_managed_slot_kind_v1 kind) noexcept {
  return kind & ~OBELISK_RT_MANAGED_SLOT_CANDIDATE;
}

uint32_t managedKindMask(obelisk_rt_managed_kind_v1 kind) noexcept {
  switch (kind) {
  case OBELISK_RT_MANAGED_CLASS:
    return OBELISK_RT_MANAGED_ROOT_KIND_CLASS;
  case OBELISK_RT_MANAGED_STRING:
    return OBELISK_RT_MANAGED_ROOT_KIND_STRING;
  case OBELISK_RT_MANAGED_CONTAINER:
    return OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER;
  case OBELISK_RT_MANAGED_REFERENCE_PATH:
    return OBELISK_RT_MANAGED_ROOT_KIND_REFERENCE_PATH;
  default:
    return 0;
  }
}

ObjectMetadata *candidateRootMetadata(ManagedHeap *heap,
                                      obelisk_rt_managed_word_v1 word,
                                      uint32_t allowedKinds) noexcept {
  if (allowedKinds == 0 ||
      (allowedKinds & ~OBELISK_RT_MANAGED_ROOT_KIND_ALL) != 0)
    return nullptr;
  if (validImmediateManagedWord(word))
    return nullptr;
  obelisk_rt_object_v1 *object = managedWordObject(word);
  if (!object)
    return nullptr;
  // Candidate words are arbitrary source bits. Keep the global registry read
  // lock through every metadata inspection so a foreign heap cannot remove
  // and destroy a coincidentally addressed allocation between lookup and
  // classification. Same-heap objects remain stable after return because the
  // caller is either the owning stop-the-world collector or an active lane.
  std::shared_lock<std::shared_mutex> lock(metadataRegistryMutex);
  auto found = metadataRegistry.find(object);
  if (found == metadataRegistry.end())
    return nullptr;
  ObjectMetadata *metadata = found->second;
  return metadata && metadata->object == object && metadata->heap == heap &&
                 metadata->allocated.load(std::memory_order_acquire) &&
                 (managedKindMask(metadata->kind) & allowedKinds) != 0
             ? metadata
             : nullptr;
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

bool rangesOverlap(uint64_t leftOffset, uint64_t leftSize, uint64_t rightOffset,
                   uint64_t rightSize) {
  return leftOffset < rightOffset + rightSize &&
         rightOffset < leftOffset + leftSize;
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
    if (entry.count == 0 || (entry.count > 1 && entry.stride == 0))
      return false;
    uint64_t elementSize = sizeof(obelisk_rt_object_v1 *);
    if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
      if (!entry.child_layout ||
          entry.slot_kind != OBELISK_RT_MANAGED_SLOT_INVALID)
        return false;
      elementSize = entry.child_layout->size;
    } else {
      bool candidate = isCandidateSlotKind(entry.slot_kind);
      uint32_t candidateMask = candidateSlotMask(entry.slot_kind);
      if ((entry.kind != OBELISK_RT_TRACE_STRONG &&
           entry.kind != OBELISK_RT_TRACE_WEAK) ||
          entry.child_layout ||
          (candidate
               ? entry.kind != OBELISK_RT_TRACE_STRONG || candidateMask == 0 ||
                     (candidateMask & ~OBELISK_RT_MANAGED_ROOT_KIND_ALL) != 0
               : entry.slot_kind < OBELISK_RT_MANAGED_SLOT_CLASS ||
                     entry.slot_kind > OBELISK_RT_MANAGED_SLOT_REFERENCE_PATH ||
                     (entry.kind == OBELISK_RT_TRACE_WEAK &&
                      entry.slot_kind != OBELISK_RT_MANAGED_SLOT_CLASS)))
        return false;
    }
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

bool traceLayoutContainsWeak(const obelisk_rt_trace_layout_v1 *layout) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    if (entry.kind == OBELISK_RT_TRACE_WEAK ||
        (entry.kind == OBELISK_RT_TRACE_EMBEDDED &&
         traceLayoutContainsWeak(entry.child_layout)))
      return true;
  }
  return false;
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
      } else if (!isCandidateSlotKind(entry.slot_kind) &&
                 offset == wantedOffset) {
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
                             uint64_t baseOffset, uint64_t wantedOffset,
                             obelisk_rt_managed_slot_kind_v1 wantedKind =
                                 OBELISK_RT_MANAGED_SLOT_INVALID) {
  if (!layout)
    return false;
  for (uint64_t index = 0; index != layout->entry_count; ++index) {
    const obelisk_rt_trace_entry_v1 &entry = layout->entries[index];
    for (uint64_t item = 0; item != entry.count; ++item) {
      uint64_t offset = baseOffset + entry.offset + item * entry.stride;
      if (entry.kind == OBELISK_RT_TRACE_EMBEDDED) {
        if (layoutHasStrongHandleAt(entry.child_layout, offset, wantedOffset,
                                    wantedKind))
          return true;
      } else if (entry.kind == OBELISK_RT_TRACE_STRONG &&
                 offset == wantedOffset &&
                 (wantedKind == OBELISK_RT_MANAGED_SLOT_INVALID ||
                  entry.slot_kind == wantedKind)) {
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
        if (!layoutHasStrongHandleAt(candidate, 0, offset, entry.slot_kind))
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
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, data + fieldOffset - offset, sizeof(word));
      if (word == 0)
        continue;
      if (isCandidateSlotKind(entry.slot_kind)) {
        if (ObjectMetadata *candidate = candidateRootMetadata(
                heap, word, candidateSlotMask(entry.slot_kind));
            candidate && referents)
          referents->push_back(
              static_cast<obelisk_rt_object_v1 *>(candidate->object));
        continue;
      }
      if (entry.slot_kind == OBELISK_RT_MANAGED_SLOT_STRING &&
          validImmediateManagedWord(word))
        continue;
      obelisk_rt_object_v1 *referent = managedWordObject(word);
      ObjectMetadata *referentMetadata = metadataFor(referent);
      obelisk_rt_managed_kind_v1 expectedKind =
          entry.slot_kind == OBELISK_RT_MANAGED_SLOT_CLASS
              ? OBELISK_RT_MANAGED_CLASS
              : (entry.slot_kind == OBELISK_RT_MANAGED_SLOT_STRING
                     ? OBELISK_RT_MANAGED_STRING
                     : (entry.slot_kind == OBELISK_RT_MANAGED_SLOT_CONTAINER
                            ? OBELISK_RT_MANAGED_CONTAINER
                            : OBELISK_RT_MANAGED_REFERENCE_PATH));
      if (!referentMetadata || referentMetadata->heap != heap ||
          referentMetadata->kind != expectedKind)
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
  std::atomic<obelisk_rt_gc_managed_root_v1 *> managedRoots{nullptr};
  std::atomic<obelisk_rt_gc_managed_root_range_v1 *> managedRootRanges{nullptr};
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
    enumerateAllocated(
        [](ObjectMetadata *metadata) { unregisterMetadata(metadata); });
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
        lane->managedRoots.load(std::memory_order_acquire) ||
        lane->managedRootRanges.load(std::memory_order_acquire) ||
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
        lane->managedRoots.load(std::memory_order_acquire) ||
        lane->managedRootRanges.load(std::memory_order_acquire) ||
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

  bool validManagedRootWord(obelisk_rt_managed_word_v1 word) const {
    if (word == 0 || validImmediateManagedWord(word))
      return true;
    obelisk_rt_object_v1 *object = managedWordObject(word);
    ObjectMetadata *metadata = metadataFor(object);
    return metadata && metadata->heap == this;
  }

  obelisk_rt_status pushManagedRoot(obelisk_rt_gc_lane_v1 *lane,
                                    obelisk_rt_gc_managed_root_v1 *root,
                                    obelisk_rt_managed_word_v1 *slot) {
    if (!activeOwner(lane) || !root || !slot || root->cookie != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (!validManagedRootWord(*slot))
      return OBELISK_RT_INVALID_HANDLE;
    root->slot = slot;
    root->previous = lane->managedRoots.load(std::memory_order_relaxed);
    root->cookie = kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
                   UINT64_C(0x4d414e41474544);
    lane->managedRoots.store(root, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status popManagedRoot(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_gc_managed_root_v1 *root) {
    if (!activeOwner(lane) || !root ||
        root->cookie != (kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
                         UINT64_C(0x4d414e41474544)) ||
        lane->managedRoots.load(std::memory_order_acquire) != root)
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->managedRoots.store(root->previous, std::memory_order_release);
    root->slot = nullptr;
    root->previous = nullptr;
    root->cookie = 0;
    return OBELISK_RT_OK;
  }

  obelisk_rt_status
  pushManagedRootRange(obelisk_rt_gc_lane_v1 *lane,
                       obelisk_rt_gc_managed_root_range_v1 *range,
                       obelisk_rt_managed_word_v1 *slots, uint64_t count) {
    if (!activeOwner(lane) || !range || (!slots && count != 0) ||
        range->cookie != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    for (uint64_t index = 0; index != count; ++index)
      if (!validManagedRootWord(slots[index]))
        return OBELISK_RT_INVALID_HANDLE;
    range->slots = slots;
    range->count = count;
    range->previous = lane->managedRootRanges.load(std::memory_order_relaxed);
    range->cookie = kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
                    reinterpret_cast<uintptr_t>(range) ^
                    UINT64_C(0x4d414e41474544);
    lane->managedRootRanges.store(range, std::memory_order_release);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status
  popManagedRootRange(obelisk_rt_gc_lane_v1 *lane,
                      obelisk_rt_gc_managed_root_range_v1 *range) {
    if (!activeOwner(lane) || !range ||
        range->cookie !=
            (kRootCookie ^ reinterpret_cast<uintptr_t>(lane) ^
             reinterpret_cast<uintptr_t>(range) ^ UINT64_C(0x4d414e41474544)) ||
        lane->managedRootRanges.load(std::memory_order_acquire) != range)
      return OBELISK_RT_INVALID_LIFECYCLE;
    lane->managedRootRanges.store(range->previous, std::memory_order_release);
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

  obelisk_rt_status registerCandidateStatic(obelisk_rt_managed_word_v1 *slot,
                                            uint32_t allowedKinds,
                                            bool activeCaller) {
    if (!slot || allowedKinds == 0 ||
        (allowedKinds & ~OBELISK_RT_MANAGED_ROOT_KIND_ALL) != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> worldLock(worldMutex, std::defer_lock);
    if (!activeCaller)
      worldLock.lock();
    std::lock_guard<std::mutex> rootLock(rootMutex);
    if (std::any_of(
            candidateStaticRoots.begin(), candidateStaticRoots.end(),
            [&](const CandidateStaticRoot &root) { return root.slot == slot; }))
      return OBELISK_RT_INVALID_LIFECYCLE;
    candidateStaticRoots.push_back({slot, allowedKinds});
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
    return guarded(context, [&] {
      if (!descriptor)
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
      return allocateImpl(
          lane, OBELISK_RT_MANAGED_CLASS, descriptor->instance_size,
          descriptor->instance_alignment, descriptor, outObject);
    });
  }

  obelisk_rt_status allocateManaged(obelisk_rt_gc_lane_v1 *lane,
                                    obelisk_rt_managed_kind_v1 kind,
                                    uint64_t extent, uint64_t alignment,
                                    const void *runtimeDescriptor,
                                    obelisk_rt_object_v1 **outObject) noexcept {
    return guarded(context, [&] {
      if (kind <= OBELISK_RT_MANAGED_CLASS ||
          kind > OBELISK_RT_MANAGED_REFERENCE_PATH || !runtimeDescriptor ||
          extent < sizeof(void *) || !validPowerOfTwo(alignment) ||
          alignment > UINT32_MAX)
        return OBELISK_RT_INVALID_ARGUMENT;
      return allocateImpl(lane, kind, extent, alignment, runtimeDescriptor,
                          outObject);
    });
  }

private:
  obelisk_rt_status allocateImpl(obelisk_rt_gc_lane_v1 *lane,
                                 obelisk_rt_managed_kind_v1 kind,
                                 uint64_t extent, uint64_t alignment,
                                 const void *runtimeDescriptor,
                                 obelisk_rt_object_v1 **outObject) {
    if (!outObject)
      return OBELISK_RT_INVALID_ARGUMENT;
    *outObject = nullptr;
    if (!activeOwner(lane))
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t objectOffset = roundUp(sizeof(SlotPrefix), alignment);
    uint64_t allocationAlignment = std::max<uint64_t>(16, alignment);
    if (extent > std::numeric_limits<uint64_t>::max() - objectOffset ||
        objectOffset + extent >
            std::numeric_limits<uint64_t>::max() - allocationAlignment + 1)
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

    uint64_t allocationSize =
        roundUp(objectOffset + extent, allocationAlignment);
    std::optional<uint64_t> objectIdentity = acquireObjectIdentity();
    if (!objectIdentity)
      return OBELISK_RT_OUT_OF_RESOURCES;
    // A safepoint above may have participated in a collection, invalidating
    // every cached span. Reacquire the TLS cache at the post-safepoint epoch
    // before dereferencing a span pointer.
    uint64_t currentEpoch = allocatorEpoch.load(std::memory_order_acquire);
    ThreadAllocationCache &cache = allocationCache(this, id, currentEpoch);
    ObjectMetadata *metadata = nullptr;
    uint8_t *slotMemory = nullptr;
    if (allocationSize <= kMaximumSmallSlot &&
        allocationAlignment <= kChunkAlignment) {
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
      auto large = std::make_unique<LargeAllocation>(allocationSize,
                                                     allocationAlignment);
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
    auto *object =
        reinterpret_cast<obelisk_rt_object_v1 *>(slotMemory + objectOffset);
    auto *prefix = reinterpret_cast<SlotPrefix *>(
        reinterpret_cast<uint8_t *>(object) - sizeof(SlotPrefix));
    prefix->metadata = metadata;
    prefix->magic = kObjectMagic;
    metadata->object = object;
    metadata->descriptor =
        kind == OBELISK_RT_MANAGED_CLASS
            ? static_cast<const obelisk_rt_class_descriptor_v1 *>(
                  runtimeDescriptor)
            : nullptr;
    metadata->identity = *objectIdentity;
    metadata->extent = extent;
    metadata->allocationSize = allocationSize;
    metadata->kind = kind;
    metadata->alignment = static_cast<uint32_t>(alignment);
    metadata->pins.store(0, std::memory_order_relaxed);
    metadata->nextTicket.store(0, std::memory_order_relaxed);
    metadata->servingTicket.store(0, std::memory_order_relaxed);
    metadata->marked = false;
    std::memcpy(object, &runtimeDescriptor, sizeof(runtimeDescriptor));
    metadata->allocated.store(true, std::memory_order_release);
    registerMetadata(metadata);

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
                                lane->state.load(std::memory_order_acquire) ==
                                    LaneState::Active;
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
  obelisk_rt_context *ownerContext() const { return context; }

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
        else {
          obelisk_rt_managed_word_v1 word = 0;
          std::memcpy(&word, address, sizeof(word));
          if (isCandidateSlotKind(entry.slot_kind))
            mark(candidateRootMetadata(this, word,
                                       candidateSlotMask(entry.slot_kind)),
                 pending);
          else
            markObject(managedWordObject(word), pending);
        }
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
    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      for (obelisk_rt_gc_managed_root_v1 *root =
               lane->managedRoots.load(std::memory_order_acquire);
           root; root = root->previous)
        markObject(root->slot ? managedWordObject(*root->slot) : nullptr,
                   pending);
    for (obelisk_rt_gc_lane_v1 *lane : lanes)
      for (obelisk_rt_gc_managed_root_range_v1 *range =
               lane->managedRootRanges.load(std::memory_order_acquire);
           range; range = range->previous)
        for (uint64_t index = 0; index != range->count; ++index)
          markObject(range->slots ? managedWordObject(range->slots[index])
                                  : nullptr,
                     pending);
    struct ProviderVisitor {
      ManagedHeap *heap;
      std::vector<ObjectMetadata *> *pending;
    } providerVisitor{this, &pending};
    auto visitProviderRoot = [](void *environment,
                                obelisk_rt_object_v1 **slot) {
      auto *visitor = static_cast<ProviderVisitor *>(environment);
      obelisk_rt_managed_word_v1 word = 0;
      if (slot)
        std::memcpy(&word, slot, sizeof(word));
      visitor->heap->markObject(managedWordObject(word), *visitor->pending);
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
      for (const CandidateStaticRoot &root : candidateStaticRoots) {
        obelisk_rt_managed_word_v1 word = root.slot ? *root.slot : 0;
        mark(candidateRootMetadata(this, word, root.allowedKinds), pending);
      }
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
      if (metadata->kind == OBELISK_RT_MANAGED_CLASS) {
        traceLayout(static_cast<uint8_t *>(metadata->object),
                    metadata->descriptor->layout, metadata, pending, weakSlots);
      } else {
        struct RuntimeTraceVisitor {
          ManagedHeap *heap;
          std::vector<ObjectMetadata *> *pending;
        } visitor{this, &pending};
        auto visit = [](void *environment, obelisk_rt_object_v1 *object) {
          auto *visitor = static_cast<RuntimeTraceVisitor *>(environment);
          obelisk_rt_managed_word_v1 word =
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(object));
          visitor->heap->markObject(managedWordObject(word), *visitor->pending);
        };
        obelisk_rt_managed_trace_runtime_object(
            metadata->kind, static_cast<uint8_t *>(metadata->object),
            metadata->extent, visit, &visitor);
      }
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
            currentLiveBytes += metadata.allocationSize;
            continue;
          }
          metadata.allocated.store(false, std::memory_order_release);
          unregisterMetadata(&metadata);
          auto *prefix = reinterpret_cast<SlotPrefix *>(
              static_cast<uint8_t *>(metadata.object) - sizeof(SlotPrefix));
          prefix->magic = 0;
          uint32_t previous = span.freeHead;
          std::memcpy(span.memory + uint64_t(slot) * span.slotSize, &previous,
                      sizeof(previous));
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
        currentLiveBytes += metadata.allocationSize;
        ++it;
        continue;
      }
      metadata.allocated.store(false, std::memory_order_release);
      unregisterMetadata(&metadata);
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
  struct CandidateStaticRoot {
    obelisk_rt_managed_word_v1 *slot;
    uint32_t allowedKinds;
  };
  std::vector<CandidateStaticRoot> candidateStaticRoots;
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
  return metadata && metadata->kind == OBELISK_RT_MANAGED_CLASS
             ? metadata->descriptor
             : nullptr;
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

obelisk_rt_context *
obelisk_rt_managed_lane_context(const obelisk_rt_gc_lane_v1 *lane) noexcept {
  return lane ? lane->context : nullptr;
}

const obelisk_rt_element_type_v1 *
obelisk_rt_managed_element_type_lookup(obelisk_rt_context *context,
                                       uint64_t typeID) {
  if (!context || typeID == 0)
    return nullptr;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = context->managedElementTypes.find(typeID);
  return found == context->managedElementTypes.end() ? nullptr : found->second;
}

obelisk_rt_status
obelisk_rt_managed_allocate(obelisk_rt_gc_lane_v1 *lane,
                            obelisk_rt_managed_kind_v1 kind, uint64_t extent,
                            uint64_t alignment, const void *runtimeDescriptor,
                            obelisk_rt_object_v1 **outObject) {
  return lane && lane->heap
             ? lane->heap->allocateManaged(lane, kind, extent, alignment,
                                           runtimeDescriptor, outObject)
             : OBELISK_RT_INVALID_ARGUMENT;
}

obelisk_rt_status obelisk_rt_managed_object_access(
    obelisk_rt_object_v1 *object, obelisk_rt_managed_kind_v1 expectedKind,
    ManagedObjectAccess access, void *environment) {
  if (!access)
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata)
    return OBELISK_RT_INVALID_HANDLE;
  if (expectedKind != OBELISK_RT_MANAGED_INVALID &&
      metadata->kind != expectedKind)
    return OBELISK_RT_INVALID_HANDLE;
  ObjectLock lock(metadata);
  return access(environment, static_cast<uint8_t *>(metadata->object),
                metadata->extent);
}

obelisk_rt_managed_kind_v1
obelisk_rt_managed_object_kind(const obelisk_rt_object_v1 *object) noexcept {
  ObjectMetadata *metadata = metadataFor(object);
  return metadata ? metadata->kind
                  : static_cast<obelisk_rt_managed_kind_v1>(
                        OBELISK_RT_MANAGED_INVALID);
}

uint64_t
obelisk_rt_managed_object_extent(const obelisk_rt_object_v1 *object) noexcept {
  ObjectMetadata *metadata = metadataFor(object);
  return metadata ? metadata->extent : 0;
}

obelisk_rt_context *
obelisk_rt_managed_object_context(const obelisk_rt_object_v1 *object) noexcept {
  ObjectMetadata *metadata = metadataFor(object);
  return metadata && metadata->heap ? metadata->heap->ownerContext() : nullptr;
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
        (current->interface_count == 0) != (current->interfaces == nullptr) ||
        (current->method_count == 0) != (current->methods == nullptr) ||
        (current->debug_name_size == 0) != (current->debug_name == nullptr) ||
        (derived &&
         ((current->flags & OBELISK_RT_CLASS_FINAL) != 0 ||
          derived->instance_size < current->instance_size ||
          derived->instance_alignment < current->instance_alignment ||
          derived->method_count < current->method_count)))
      return OBELISK_RT_INVALID_DESIGN;
    for (uint64_t index = 0; index != current->interface_count; ++index) {
      const obelisk_rt_interface_descriptor_v1 &interface =
          current->interfaces[index];
      if (interface.interface_id == 0 ||
          (interface.method_count == 0) !=
              (interface.method_slots == nullptr)) {
        return OBELISK_RT_INVALID_DESIGN;
      }
      if (index != 0 &&
          current->interfaces[index - 1].interface_id >= interface.interface_id)
        return OBELISK_RT_INVALID_DESIGN;
      for (uint64_t ordinal = 0; ordinal != interface.method_count; ++ordinal) {
        uint32_t slot = interface.method_slots[ordinal];
        if (slot == UINT32_MAX) {
          if ((current->flags & OBELISK_RT_CLASS_ABSTRACT) == 0)
            return OBELISK_RT_INVALID_DESIGN;
        } else if (slot >= current->method_count) {
          return OBELISK_RT_INVALID_DESIGN;
        } else if ((current->flags & OBELISK_RT_CLASS_ABSTRACT) == 0 &&
                   (current->methods[slot].flags & OBELISK_RT_METHOD_PURE) !=
                       0) {
          return OBELISK_RT_INVALID_DESIGN;
        }
      }
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
    if (const obelisk_rt_random_layout_v1 *random = current->random_layout) {
      if (random->version != OBELISK_RT_VERSION || random->reserved != 0 ||
          (random->edge_count == 0) != (random->edges == nullptr) ||
          (random->variable_count == 0) != (random->variables == nullptr) ||
          (random->edge_count == 0 && random->variable_count == 0))
        return OBELISK_RT_INVALID_DESIGN;
      for (uint64_t index = 0; index != random->edge_count; ++index) {
        const obelisk_rt_random_edge_v1 &edge = random->edges[index];
        if (edge.handle_offset % alignof(obelisk_rt_object_v1 *) != 0 ||
            edge.mode_offset % alignof(uint64_t) != 0 ||
            !checkedRange(edge.handle_offset, sizeof(obelisk_rt_object_v1 *),
                          current->instance_size) ||
            !checkedRange(edge.mode_offset, sizeof(uint64_t),
                          current->instance_size) ||
            !validPowerOfTwo(edge.mode_mask) ||
            !layoutHasStrongHandleAt(current->layout, 0, edge.handle_offset,
                                     OBELISK_RT_MANAGED_SLOT_CLASS))
          return OBELISK_RT_INVALID_DESIGN;
        for (uint64_t previous = 0; previous != index; ++previous)
          if (random->edges[previous].handle_offset == edge.handle_offset ||
              (random->edges[previous].mode_offset == edge.mode_offset &&
               random->edges[previous].mode_mask == edge.mode_mask))
            return OBELISK_RT_INVALID_DESIGN;
      }
      for (uint64_t index = 0; index != random->variable_count; ++index) {
        const obelisk_rt_random_variable_v1 &variable =
            random->variables[index];
        bool isRandC = (variable.flags & OBELISK_RT_RANDOM_VARIABLE_RANDC) != 0;
        uint64_t valueSize = (uint64_t(variable.bit_width) + 7) / 8;
        uint64_t storageSize =
            (variable.flags & OBELISK_RT_RANDOM_VARIABLE_FOUR_STATE) != 0
                ? valueSize * 2
                : valueSize;
        if (variable.bit_width == 0 ||
            (variable.flags & ~kRandomVariableFlags) != 0 ||
            variable.value_offset < sizeof(void *) ||
            variable.mode_offset < sizeof(void *) ||
            variable.mode_offset % alignof(uint64_t) != 0 ||
            !checkedRange(variable.value_offset, storageSize,
                          current->instance_size) ||
            !checkedRange(variable.mode_offset, sizeof(uint64_t),
                          current->instance_size) ||
            !validPowerOfTwo(variable.mode_mask) ||
            rangesOverlap(variable.value_offset, storageSize,
                          variable.mode_offset, sizeof(uint64_t)) ||
            layoutOverlapsHandle(current->layout, 0, variable.value_offset,
                                 storageSize) ||
            (isRandC != (variable.randc_key_offset != UINT64_MAX)) ||
            (isRandC != (variable.randc_position_offset != UINT64_MAX)))
          return OBELISK_RT_INVALID_DESIGN;
        if (isRandC &&
            (variable.randc_key_offset < sizeof(void *) ||
             variable.randc_position_offset < sizeof(void *) ||
             variable.randc_key_offset % alignof(uint64_t) != 0 ||
             variable.randc_position_offset % alignof(uint64_t) != 0 ||
             variable.randc_key_offset == variable.randc_position_offset ||
             rangesOverlap(variable.value_offset, storageSize,
                           variable.randc_key_offset, sizeof(uint64_t)) ||
             rangesOverlap(variable.value_offset, storageSize,
                           variable.randc_position_offset, sizeof(uint64_t)) ||
             rangesOverlap(variable.mode_offset, sizeof(uint64_t),
                           variable.randc_key_offset, sizeof(uint64_t)) ||
             rangesOverlap(variable.mode_offset, sizeof(uint64_t),
                           variable.randc_position_offset, sizeof(uint64_t)) ||
             !checkedRange(variable.randc_key_offset, sizeof(uint64_t),
                           current->instance_size) ||
             !checkedRange(variable.randc_position_offset, sizeof(uint64_t),
                           current->instance_size) ||
             layoutOverlapsHandle(current->layout, 0, variable.randc_key_offset,
                                  sizeof(uint64_t)) ||
             layoutOverlapsHandle(current->layout, 0,
                                  variable.randc_position_offset,
                                  sizeof(uint64_t))))
          return OBELISK_RT_INVALID_DESIGN;
        for (uint64_t previous = 0; previous != index; ++previous) {
          const obelisk_rt_random_variable_v1 &other =
              random->variables[previous];
          uint64_t otherValueSize = (uint64_t(other.bit_width) + 7) / 8;
          uint64_t otherStorageSize =
              (other.flags & OBELISK_RT_RANDOM_VARIABLE_FOUR_STATE) != 0
                  ? otherValueSize * 2
                  : otherValueSize;
          bool otherIsRandC =
              (other.flags & OBELISK_RT_RANDOM_VARIABLE_RANDC) != 0;
          if (rangesOverlap(other.value_offset, otherStorageSize,
                            variable.value_offset, storageSize) ||
              (isRandC &&
               (rangesOverlap(other.value_offset, otherStorageSize,
                              variable.randc_key_offset, sizeof(uint64_t)) ||
                rangesOverlap(other.value_offset, otherStorageSize,
                              variable.randc_position_offset,
                              sizeof(uint64_t)))) ||
              (otherIsRandC &&
               (rangesOverlap(variable.value_offset, storageSize,
                              other.randc_key_offset, sizeof(uint64_t)) ||
                rangesOverlap(variable.value_offset, storageSize,
                              other.randc_position_offset,
                              sizeof(uint64_t)))) ||
              (isRandC && otherIsRandC &&
               (rangesOverlap(variable.randc_key_offset, sizeof(uint64_t),
                              other.randc_key_offset, sizeof(uint64_t)) ||
                rangesOverlap(variable.randc_key_offset, sizeof(uint64_t),
                              other.randc_position_offset, sizeof(uint64_t)) ||
                rangesOverlap(variable.randc_position_offset, sizeof(uint64_t),
                              other.randc_key_offset, sizeof(uint64_t)) ||
                rangesOverlap(variable.randc_position_offset, sizeof(uint64_t),
                              other.randc_position_offset,
                              sizeof(uint64_t)))) ||
              (other.mode_offset == variable.mode_offset &&
               other.mode_mask == variable.mode_mask))
            return OBELISK_RT_INVALID_DESIGN;
        }
        for (uint64_t edgeIndex = 0; edgeIndex != random->edge_count;
             ++edgeIndex) {
          const obelisk_rt_random_edge_v1 &edge = random->edges[edgeIndex];
          if (edge.mode_offset == variable.mode_offset &&
              edge.mode_mask == variable.mode_mask)
            return OBELISK_RT_INVALID_DESIGN;
        }
      }
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
obelisk_rt_v1_gc_managed_root_push(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_gc_managed_root_v1 *root,
                                   obelisk_rt_managed_word_v1 *slot) {
  return lane && lane->heap ? lane->heap->pushManagedRoot(lane, root, slot)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_gc_managed_root_pop(obelisk_rt_gc_lane_v1 *lane,
                                  obelisk_rt_gc_managed_root_v1 *root) {
  return lane && lane->heap ? lane->heap->popManagedRoot(lane, root)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status obelisk_rt_v1_gc_managed_root_range_push(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_gc_managed_root_range_v1 *range,
    obelisk_rt_managed_word_v1 *slots, uint64_t count) {
  return lane && lane->heap
             ? lane->heap->pushManagedRootRange(lane, range, slots, count)
             : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_status obelisk_rt_v1_gc_managed_root_range_pop(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_gc_managed_root_range_v1 *range) {
  return lane && lane->heap ? lane->heap->popManagedRootRange(lane, range)
                            : OBELISK_RT_INVALID_ARGUMENT;
}

extern "C" obelisk_rt_managed_word_v1 obelisk_rt_v1_gc_candidate_root(
    obelisk_rt_context *context, obelisk_rt_managed_word_v1 word,
    obelisk_rt_managed_root_kind_mask_v1 allowedKinds) {
  if ((allowedKinds & OBELISK_RT_MANAGED_ROOT_KIND_STRING) != 0 &&
      validImmediateManagedWord(word))
    return word;
  ManagedHeap *heap = heapFor(context);
  return heap && candidateRootMetadata(heap, word, allowedKinds) ? word : 0;
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

extern "C" obelisk_rt_status obelisk_rt_v1_gc_candidate_static_root_register(
    obelisk_rt_context *context, obelisk_rt_managed_word_v1 *slot,
    obelisk_rt_managed_root_kind_mask_v1 allowedKinds) {
  ManagedHeap *heap = heapFor(context);
  bool activeCaller = heap && heap->hasActiveCaller();
  return heap ? guarded(context,
                        [&] {
                          return heap->registerCandidateStatic(
                              slot, allowedKinds, activeCaller);
                        })
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
obelisk_rt_v1_random_graph_discover(obelisk_rt_gc_lane_v1 *lane,
                                    obelisk_rt_object_v1 *root,
                                    obelisk_rt_random_graph_v1 **outGraph) {
  if (!outGraph)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outGraph = nullptr;
  if (!lane || !lane->heap || !lane->heap->activeOwner(lane) || !root)
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *rootMetadata = metadataFor(root);
  if (!rootMetadata || rootMetadata->heap != lane->heap ||
      rootMetadata->kind != OBELISK_RT_MANAGED_CLASS)
    return OBELISK_RT_INVALID_HANDLE;

  std::unique_ptr<obelisk_rt_random_graph_v1> graph;
  try {
    graph = std::make_unique<obelisk_rt_random_graph_v1>();
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  graph->context = lane->context;
  std::unordered_set<uint64_t> seen;
  auto rollback = [&] {
    for (const auto &binding : graph->objects)
      (void)lane->heap->unpin(binding.object, /*activeCaller=*/true);
    graph->objects.clear();
  };
  auto append = [&](obelisk_rt_object_v1 *object,
                    const obelisk_rt_class_descriptor_v1 *descriptor)
      -> obelisk_rt_status {
    obelisk_rt_status status = lane->heap->pin(object, /*activeCaller=*/true);
    if (status != OBELISK_RT_OK)
      return status;
    try {
      graph->objects.push_back({object, descriptor});
    } catch (...) {
      (void)lane->heap->unpin(object, /*activeCaller=*/true);
      throw;
    }
    return OBELISK_RT_OK;
  };

  try {
    seen.insert(rootMetadata->identity);
    obelisk_rt_status status = append(root, rootMetadata->descriptor);
    if (status != OBELISK_RT_OK)
      return status;
    for (size_t cursor = 0; cursor != graph->objects.size(); ++cursor) {
      const auto &binding = graph->objects[cursor];
      obelisk_rt_object_v1 *object = binding.object;
      ObjectMetadata *metadata = metadataFor(object);
      if (!metadata || metadata->heap != lane->heap ||
          metadata->kind != OBELISK_RT_MANAGED_CLASS ||
          metadata->descriptor != binding.descriptor) {
        rollback();
        return OBELISK_RT_INVALID_HANDLE;
      }
      std::vector<const obelisk_rt_class_descriptor_v1 *> hierarchy;
      for (const obelisk_rt_class_descriptor_v1 *descriptor =
               metadata->descriptor;
           descriptor; descriptor = descriptor->base)
        hierarchy.push_back(descriptor);
      for (auto descriptorIt = hierarchy.rbegin();
           descriptorIt != hierarchy.rend(); ++descriptorIt) {
        const obelisk_rt_class_descriptor_v1 *descriptor = *descriptorIt;
        const obelisk_rt_random_layout_v1 *random = descriptor->random_layout;
        if (!random)
          continue;
        for (uint64_t index = 0; index != random->variable_count; ++index) {
          const obelisk_rt_random_variable_v1 &variable =
              random->variables[index];
          uint64_t mode = 0;
          {
            ObjectLock lock(metadata);
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(object);
            std::memcpy(&mode, bytes + variable.mode_offset, sizeof(mode));
          }
          if ((mode & variable.mode_mask) == 0)
            graph->variables.push_back({object, &variable});
        }
        for (uint64_t index = 0; index != random->edge_count; ++index) {
          const obelisk_rt_random_edge_v1 &edge = random->edges[index];
          uint64_t mode = 0;
          obelisk_rt_object_v1 *child = nullptr;
          {
            ObjectLock lock(metadata);
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(object);
            std::memcpy(&mode, bytes + edge.mode_offset, sizeof(mode));
            std::memcpy(&child, bytes + edge.handle_offset, sizeof(child));
          }
          if ((mode & edge.mode_mask) != 0 || !child)
            continue;
          ObjectMetadata *childMetadata = metadataFor(child);
          if (!childMetadata || childMetadata->heap != lane->heap ||
              childMetadata->kind != OBELISK_RT_MANAGED_CLASS) {
            rollback();
            return OBELISK_RT_INVALID_HANDLE;
          }
          if (!seen.insert(childMetadata->identity).second)
            continue;
          status = append(child, childMetadata->descriptor);
          if (status != OBELISK_RT_OK) {
            rollback();
            return status;
          }
        }
      }
    }
    *outGraph = graph.release();
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    rollback();
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    rollback();
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void
obelisk_rt_v1_random_graph_destroy(obelisk_rt_random_graph_v1 *graph) {
  if (!graph)
    return;
  for (const auto &binding : graph->objects)
    (void)obelisk_rt_v1_gc_unpin(graph->context, binding.object);
  delete graph;
}

extern "C" uint64_t
obelisk_rt_v1_random_graph_size(const obelisk_rt_random_graph_v1 *graph) {
  return graph ? graph->objects.size() : 0;
}

extern "C" obelisk_rt_object_v1 *
obelisk_rt_v1_random_graph_object(const obelisk_rt_random_graph_v1 *graph,
                                  uint64_t index) {
  return graph && index < graph->objects.size() ? graph->objects[index].object
                                                : nullptr;
}

extern "C" const obelisk_rt_class_descriptor_v1 *
obelisk_rt_v1_random_graph_object_descriptor(
    const obelisk_rt_random_graph_v1 *graph, uint64_t index) {
  return graph && index < graph->objects.size()
             ? graph->objects[index].descriptor
             : nullptr;
}

extern "C" uint64_t obelisk_rt_v1_random_graph_variable_count(
    const obelisk_rt_random_graph_v1 *graph) {
  return graph ? graph->variables.size() : 0;
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_graph_variable(
    const obelisk_rt_random_graph_v1 *graph, uint64_t index,
    obelisk_rt_object_v1 **outObject,
    const obelisk_rt_random_variable_v1 **outVariable) {
  if (!outObject || !outVariable)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outObject = nullptr;
  *outVariable = nullptr;
  if (!graph || index >= graph->variables.size())
    return OBELISK_RT_INVALID_ARGUMENT;
  const obelisk_rt_random_graph_v1::VariableBinding &binding =
      graph->variables[index];
  *outObject = binding.object;
  *outVariable = binding.variable;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_graph_resolve_variable(
    const obelisk_rt_random_graph_v1 *graph, uint64_t originIndex,
    const obelisk_rt_random_variable_reference_v1 *reference,
    obelisk_rt_object_v1 **outObject,
    const obelisk_rt_random_variable_v1 **outVariable,
    uint64_t *outGraphIndex) {
  if (!outObject || !outVariable || !outGraphIndex)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outObject = nullptr;
  *outVariable = nullptr;
  *outGraphIndex = UINT64_MAX;
  if (!graph || originIndex >= graph->objects.size() || !reference ||
      (reference->handle_count == 0) !=
          (reference->handle_offsets == nullptr) ||
      reference->bit_width == 0 ||
      (reference->flags & ~kRandomVariableFlags) != 0)
    return OBELISK_RT_INVALID_ARGUMENT;

  obelisk_rt_object_v1 *object = graph->objects[originIndex].object;
  for (uint64_t index = 0; index != reference->handle_count; ++index) {
    ObjectMetadata *metadata = metadataFor(object);
    uint64_t offset = reference->handle_offsets[index];
    if (!metadata || !metadata->heap ||
        metadata->heap->ownerContext() != graph->context ||
        metadata->kind != OBELISK_RT_MANAGED_CLASS || !metadata->descriptor ||
        !layoutHasStrongHandleAt(metadata->descriptor->layout, 0, offset,
                                 OBELISK_RT_MANAGED_SLOT_CLASS))
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_object_v1 *next = nullptr;
    {
      ObjectLock lock(metadata);
      const uint8_t *bytes = reinterpret_cast<const uint8_t *>(object);
      std::memcpy(&next, bytes + offset, sizeof(next));
    }
    if (!next)
      return OBELISK_RT_INVALID_HANDLE;
    ObjectMetadata *nextMetadata = metadataFor(next);
    if (!nextMetadata || !nextMetadata->heap ||
        nextMetadata->heap->ownerContext() != graph->context ||
        nextMetadata->kind != OBELISK_RT_MANAGED_CLASS)
      return OBELISK_RT_INVALID_HANDLE;
    object = next;
  }

  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || !metadata->heap ||
      metadata->heap->ownerContext() != graph->context ||
      metadata->kind != OBELISK_RT_MANAGED_CLASS || !metadata->descriptor)
    return OBELISK_RT_INVALID_HANDLE;
  bool activeObject = false;
  for (const auto &binding : graph->objects)
    if (binding.object == object) {
      activeObject = true;
      break;
    }
  if (!activeObject)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_random_variable_v1 *resolved = nullptr;
  for (const obelisk_rt_class_descriptor_v1 *descriptor = metadata->descriptor;
       descriptor && !resolved; descriptor = descriptor->base) {
    const obelisk_rt_random_layout_v1 *random = descriptor->random_layout;
    if (!random)
      continue;
    for (uint64_t index = 0; index != random->variable_count; ++index) {
      const obelisk_rt_random_variable_v1 &candidate = random->variables[index];
      if (candidate.value_offset == reference->value_offset &&
          candidate.bit_width == reference->bit_width &&
          candidate.flags == reference->flags) {
        resolved = &candidate;
        break;
      }
    }
  }
  if (!resolved)
    return OBELISK_RT_INVALID_ARGUMENT;

  for (uint64_t index = 0; index != graph->variables.size(); ++index) {
    const auto &binding = graph->variables[index];
    if (binding.object == object && binding.variable == resolved) {
      *outGraphIndex = index;
      break;
    }
  }
  *outObject = object;
  *outVariable = resolved;
  return OBELISK_RT_OK;
}

namespace {

bool traceLayoutsEquivalent(const obelisk_rt_trace_layout_v1 *left,
                            const obelisk_rt_trace_layout_v1 *right) {
  if (left == right)
    return true;
  if (!left || !right || left->version != right->version ||
      left->reserved != right->reserved || left->size != right->size ||
      left->alignment != right->alignment ||
      left->entry_count != right->entry_count)
    return false;
  for (uint64_t index = 0; index != left->entry_count; ++index) {
    const auto &a = left->entries[index];
    const auto &b = right->entries[index];
    if (a.offset != b.offset || a.stride != b.stride || a.count != b.count ||
        a.kind != b.kind || a.slot_kind != b.slot_kind ||
        !traceLayoutsEquivalent(a.child_layout, b.child_layout))
      return false;
  }
  return true;
}

bool elementTypesEquivalent(const obelisk_rt_element_type_v1 *left,
                            const obelisk_rt_element_type_v1 *right) {
  return left == right ||
         (left && right && left->version == right->version &&
          left->kind == right->kind && left->type_id == right->type_id &&
          left->flags == right->flags && left->reserved == right->reserved &&
          left->value_size == right->value_size &&
          left->alignment == right->alignment &&
          left->bit_width == right->bit_width &&
          traceLayoutsEquivalent(left->trace, right->trace));
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_element_type_validate(
    const obelisk_rt_element_type_v1 *descriptor) {
  constexpr uint32_t validFlags =
      OBELISK_RT_ELEMENT_FOUR_STATE | OBELISK_RT_ELEMENT_SIGNED;
  if (!descriptor || descriptor->version != OBELISK_RT_VERSION ||
      descriptor->type_id == 0 || descriptor->reserved != 0 ||
      descriptor->kind < OBELISK_RT_ELEMENT_BITS ||
      descriptor->kind > OBELISK_RT_ELEMENT_EVENT ||
      (descriptor->flags & ~validFlags) != 0 || descriptor->value_size == 0 ||
      descriptor->value_size > UINT64_MAX - 64 ||
      !validPowerOfTwo(descriptor->alignment) || descriptor->alignment > 16 ||
      descriptor->value_size % descriptor->alignment != 0 ||
      ((descriptor->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0 &&
       descriptor->value_size > UINT64_MAX / 2))
    return OBELISK_RT_INVALID_DESIGN;
  bool handleKind = descriptor->kind == OBELISK_RT_ELEMENT_CLASS_HANDLE ||
                    descriptor->kind == OBELISK_RT_ELEMENT_STRING ||
                    descriptor->kind == OBELISK_RT_ELEMENT_CONTAINER_HANDLE;
  if (handleKind && (descriptor->value_size != sizeof(obelisk_rt_object_v1 *) ||
                     descriptor->bit_width != 0 || !descriptor->trace))
    return OBELISK_RT_INVALID_DESIGN;
  if ((descriptor->kind == OBELISK_RT_ELEMENT_REAL ||
       descriptor->kind == OBELISK_RT_ELEMENT_CLASS_HANDLE ||
       descriptor->kind == OBELISK_RT_ELEMENT_STRING ||
       descriptor->kind == OBELISK_RT_ELEMENT_CONTAINER_HANDLE ||
       descriptor->kind == OBELISK_RT_ELEMENT_EVENT) &&
      (descriptor->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0)
    return OBELISK_RT_INVALID_DESIGN;
  if ((descriptor->kind == OBELISK_RT_ELEMENT_BITS ||
       descriptor->kind == OBELISK_RT_ELEMENT_LOGIC) &&
      (descriptor->bit_width == 0 || descriptor->value_size > UINT64_MAX / 8 ||
       descriptor->bit_width > descriptor->value_size * 8))
    return OBELISK_RT_INVALID_DESIGN;
  if (descriptor->trace &&
      (descriptor->trace->size != descriptor->value_size ||
       !validateTraceLayout(descriptor->trace, descriptor->value_size) ||
       traceLayoutContainsWeak(descriptor->trace)))
    return OBELISK_RT_INVALID_DESIGN;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_element_type_register(
    obelisk_rt_context *context, const obelisk_rt_element_type_v1 *descriptor) {
  if (!context ||
      obelisk_rt_v1_element_type_validate(descriptor) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto [found, inserted] = context->managedElementTypes.try_emplace(
        descriptor->type_id, descriptor);
    return inserted || elementTypesEquivalent(found->second, descriptor)
               ? OBELISK_RT_OK
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

extern "C" obelisk_rt_status obelisk_rt_v1_gc_design_candidate_root_register(
    obelisk_rt_context *context, uint64_t bitOffset,
    obelisk_rt_managed_root_kind_mask_v1 allowedKinds) {
  ManagedHeap *heap = heapFor(context);
  if (!heap || bitOffset % 64 != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t word = bitOffset / 64;
  if (word >= context->stateValue.size())
    return OBELISK_RT_INVALID_DESIGN;
  return guarded(context, [&] {
    auto *slot = reinterpret_cast<obelisk_rt_managed_word_v1 *>(
        context->stateValue.data() + word);
    return heap->registerCandidateStatic(slot, allowedKinds,
                                         heap->hasActiveCaller());
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
  if (!sourceMetadata || sourceMetadata->heap != lane->heap ||
      !sourceMetadata->descriptor)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_class_descriptor_v1 *dynamicDescriptor =
      sourceMetadata->descriptor;
  obelisk_rt_gc_root_v1 sourceRoot{};
  obelisk_rt_status status = lane->heap->pushRoot(lane, &sourceRoot, &source);
  if (status != OBELISK_RT_OK)
    return status;
  status = lane->heap->allocate(lane, dynamicDescriptor, outObject);
  if (status != OBELISK_RT_OK) {
    (void)lane->heap->popRoot(lane, &sourceRoot);
    return status;
  }
  ObjectLock lock(sourceMetadata);
  std::memcpy(reinterpret_cast<uint8_t *>(*outObject) + sizeof(void *),
              reinterpret_cast<const uint8_t *>(source) + sizeof(void *),
              dynamicDescriptor->instance_size - sizeof(void *));
  return lane->heap->popRoot(lane, &sourceRoot);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_read(obelisk_rt_object_v1 *object, uint64_t offset,
                          void *data, uint64_t size) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS ||
      (!data && size != 0) ||
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
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS ||
      (!data && size != 0) || offset < sizeof(void *) ||
      !checkedRange(offset, size, metadata->descriptor->instance_size) ||
      !validateLayoutHandleWrite(metadata->descriptor->layout, 0, offset, size,
                                 static_cast<const uint8_t *>(data),
                                 metadata->heap))
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = false;
  {
    ObjectLock lock(metadata);
    uint8_t *destination = reinterpret_cast<uint8_t *>(object) + offset;
    changed = size != 0 && std::memcmp(destination, data, size) != 0;
    std::memcpy(destination, data, size);
  }
  if (changed)
    obelisk_rt_notify_managed_watch(object, OBELISK_RT_MANAGED_WATCH_FIELD,
                                    offset);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_bits_insert(obelisk_rt_object_v1 *object, uint64_t offset,
                                 uint64_t fieldBitWidth, int64_t lowBit,
                                 uint32_t valid, uint64_t replacement,
                                 uint32_t replacementWidth) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS ||
      offset < sizeof(void *) || fieldBitWidth == 0 ||
      fieldBitWidth > static_cast<uint64_t>(INT64_MAX) ||
      replacementWidth == 0 || replacementWidth > 64 || valid > 1 ||
      !checkedRange(offset, (fieldBitWidth + 7) / 8,
                    metadata->descriptor->instance_size) ||
      layoutOverlapsHandle(metadata->descriptor->layout, 0, offset,
                           (fieldBitWidth + 7) / 8))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!valid)
    return OBELISK_RT_OK;
  if (lowBit < -static_cast<int64_t>(replacementWidth - 1) ||
      lowBit >= static_cast<int64_t>(fieldBitWidth))
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = false;
  {
    ObjectLock lock(metadata);
    uint8_t *destination = reinterpret_cast<uint8_t *>(object) + offset;
    for (uint32_t sourceBit = 0; sourceBit != replacementWidth; ++sourceBit) {
      int64_t targetBit = lowBit + sourceBit;
      if (targetBit < 0 || static_cast<uint64_t>(targetBit) >= fieldBitWidth)
        continue;
      uint8_t &byte = destination[static_cast<uint64_t>(targetBit) / 8];
      uint8_t mask = uint8_t{1} << (static_cast<unsigned>(targetBit) % 8);
      uint8_t updated =
          (replacement >> sourceBit) & 1 ? byte | mask : byte & ~mask;
      changed |= updated != byte;
      byte = updated;
    }
  }
  if (changed)
    obelisk_rt_notify_managed_watch(object, OBELISK_RT_MANAGED_WATCH_FIELD,
                                    offset);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_read_planes(obelisk_rt_object_v1 *object, uint64_t offset,
                                 void *value, void *unknown,
                                 uint64_t planeSize) {
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS || !value ||
      !unknown || planeSize == 0 || planeSize > UINT64_MAX / 2 ||
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
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS || !value ||
      !unknown || planeSize == 0 || offset < sizeof(void *) ||
      planeSize > UINT64_MAX / 2 ||
      !checkedRange(offset, planeSize * 2,
                    metadata->descriptor->instance_size) ||
      !validateLayoutHandleWrite(metadata->descriptor->layout, 0, offset,
                                 planeSize, static_cast<const uint8_t *>(value),
                                 metadata->heap) ||
      !validateLayoutHandleWrite(
          metadata->descriptor->layout, 0, offset + planeSize, planeSize,
          static_cast<const uint8_t *>(unknown), metadata->heap))
    return OBELISK_RT_INVALID_ARGUMENT;
  bool changed = false;
  {
    ObjectLock lock(metadata);
    uint8_t *destination = reinterpret_cast<uint8_t *>(object) + offset;
    changed = std::memcmp(destination, value, planeSize) != 0 ||
              std::memcmp(destination + planeSize, unknown, planeSize) != 0;
    std::memcpy(destination, value, planeSize);
    std::memcpy(destination + planeSize, unknown, planeSize);
  }
  if (changed)
    obelisk_rt_notify_managed_watch(object, OBELISK_RT_MANAGED_WATCH_FIELD,
                                    offset);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_object_field_load(obelisk_rt_object_v1 *object, uint64_t offset,
                                obelisk_rt_object_v1 **outValue) {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outValue = nullptr;
  ObjectMetadata *metadata = metadataFor(object);
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS ||
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
  if (!metadata || metadata->kind != OBELISK_RT_MANAGED_CLASS ||
      !checkedRange(offset, sizeof(value),
                    metadata->descriptor->instance_size) ||
      !layoutHasHandleAt(metadata->descriptor->layout, 0, offset))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (value) {
    ObjectMetadata *valueMetadata = metadataFor(value);
    if (!valueMetadata || valueMetadata->heap != metadata->heap)
      return OBELISK_RT_INVALID_HANDLE;
  }
  bool changed = false;
  {
    ObjectLock lock(metadata);
    uint8_t *destination = reinterpret_cast<uint8_t *>(object) + offset;
    changed = std::memcmp(destination, &value, sizeof(value)) != 0;
    std::memcpy(destination, &value, sizeof(value));
  }
  if (changed)
    obelisk_rt_notify_managed_watch(object, OBELISK_RT_MANAGED_WATCH_FIELD,
                                    offset);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_scheduler_managed_nba(
    obelisk_rt_context *context, obelisk_rt_object_v1 *destination,
    uint64_t offset, const void *value, const void *unknown, uint64_t planeSize,
    uint64_t delay) {
  ManagedHeap *heap = heapFor(context);
  ObjectMetadata *destinationMetadata = metadataFor(destination);
  bool referencePath =
      destinationMetadata &&
      destinationMetadata->kind == OBELISK_RT_MANAGED_REFERENCE_PATH &&
      offset == UINT64_MAX;
  if (!heap || !destinationMetadata ||
      (!referencePath &&
       destinationMetadata->kind != OBELISK_RT_MANAGED_CLASS) ||
      destinationMetadata->heap != heap || !value || planeSize == 0 ||
      planeSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  if (referencePath) {
    if (planeSize > UINT64_MAX / 8)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_status shape = obelisk_rt_reference_path_shape(
        destination, planeSize, planeSize * 8, unknown != nullptr, 0);
    if (shape != OBELISK_RT_OK)
      return shape;
  }
  const obelisk_rt_trace_layout_v1 *layout =
      referencePath ? nullptr : destinationMetadata->descriptor->layout;
  bool managedSlot = planeSize == sizeof(obelisk_rt_object_v1 *) &&
                     !referencePath && layoutHasHandleAt(layout, 0, offset);
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
  } else if (!referencePath) {
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
    update.referencePath = referencePath;
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
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      rollback();
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
    }
    if (context->nextSchedulerSequence == 0) {
      rollback();
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    update.execRegion = obelisk_rt_commit_region(
        context->activeHomeRegion == UINT32_MAX
            ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
            : context->activeHomeRegion);
    if (update.execRegion == UINT32_MAX) {
      rollback();
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return OBELISK_RT_INVALID_LIFECYCLE;
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
  if (!destination ||
      (update.referencePath
           ? destination->kind != OBELISK_RT_MANAGED_REFERENCE_PATH
           : destination->kind != OBELISK_RT_MANAGED_CLASS) ||
      destination->heap != heap || update.value.size() != update.planeSize ||
      (!update.unknown.empty() && update.unknown.size() != update.planeSize)) {
    status = OBELISK_RT_INVALID_HANDLE;
  } else if (update.referencePath) {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    status = lane
                 ? obelisk_rt_v1_reference_path_store(
                       lane, update.destination, update.value.data(),
                       update.unknown.empty() ? nullptr : update.unknown.data())
                 : OBELISK_RT_INVALID_LIFECYCLE;
  } else {
    bool changed = false;
    {
      ObjectLock lock(destination);
      uint8_t *base = reinterpret_cast<uint8_t *>(update.destination);
      changed = std::memcmp(base + update.offset, update.value.data(),
                            static_cast<size_t>(update.planeSize)) != 0;
      if (!update.unknown.empty())
        changed |= std::memcmp(base + update.offset + update.planeSize,
                               update.unknown.data(),
                               static_cast<size_t>(update.planeSize)) != 0;
      std::memcpy(base + update.offset, update.value.data(),
                  static_cast<size_t>(update.planeSize));
      if (!update.unknown.empty())
        std::memcpy(base + update.offset + update.planeSize,
                    update.unknown.data(),
                    static_cast<size_t>(update.planeSize));
    }
    if (changed)
      obelisk_rt_notify_managed_watch(
          update.destination, OBELISK_RT_MANAGED_WATCH_FIELD, update.offset);
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
      if (current->interfaces[index].interface_id == target->class_id)
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

namespace {

std::optional<uint64_t> managedWatchSelector(obelisk_rt_managed_watch_kind kind,
                                             uint64_t selector) {
  if (kind == OBELISK_RT_MANAGED_WATCH_FIELD)
    return selector;
  if (kind == OBELISK_RT_MANAGED_WATCH_CONTAINER_SIZE)
    return UINT64_MAX;
  return std::nullopt;
}

} // namespace

extern "C" uint64_t
obelisk_rt_v1_managed_watch(obelisk_rt_object_v1 *object,
                            obelisk_rt_managed_watch_kind kind,
                            uint64_t selector) {
  ObjectMetadata *metadata = metadataFor(object);
  std::optional<uint64_t> key = managedWatchSelector(kind, selector);
  if (!metadata || !metadata->heap || !key)
    return 0;
  obelisk_rt_context *context = metadata->heap->ownerContext();
  if (!context || metadata->identity == 0)
    return 0;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto &token = context->managedWatchTokens[metadata->identity][*key];
  if (token != 0)
    return token;
  if (context->nextManagedWatchToken == 0)
    return 0;
  token = context->nextManagedWatchToken++;
  return token;
}

void obelisk_rt_notify_managed_watch(obelisk_rt_object_v1 *object,
                                     obelisk_rt_managed_watch_kind kind,
                                     uint64_t selector) {
  ObjectMetadata *metadata = metadataFor(object);
  std::optional<uint64_t> key = managedWatchSelector(kind, selector);
  if (!metadata || !metadata->heap || !key || metadata->identity == 0)
    return;
  obelisk_rt_context *context = metadata->heap->ownerContext();
  if (!context)
    return;
  std::lock_guard<std::recursive_mutex> transaction(context->transactionMutex);
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto objectWatch = context->managedWatchTokens.find(metadata->identity);
  if (objectWatch == context->managedWatchTokens.end())
    return;
  auto watched = objectWatch->second.find(*key);
  if (watched == objectWatch->second.end() || watched->second == 0)
    return;
  if (!obelisk_rt_notify_observer_managed_unlocked(context, watched->second) &&
      context->schedulerStatus == OBELISK_RT_OK)
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
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

extern "C" obelisk_rt_status obelisk_rt_v1_interface_method_resolve(
    obelisk_rt_object_v1 *receiver, uint64_t interfaceID,
    uint64_t interfaceOrdinal, uint64_t signatureID,
    const obelisk_rt_method_descriptor_v1 **outMethod) {
  const obelisk_rt_class_descriptor_v1 *descriptor = descriptorFor(receiver);
  if (!descriptor || !outMethod || interfaceID == 0)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_interface_descriptor_v1 *interface = nullptr;
  for (const obelisk_rt_class_descriptor_v1 *current = descriptor; current;
       current = current->base) {
    uint64_t begin = 0, end = current->interface_count;
    while (begin != end) {
      uint64_t middle = begin + (end - begin) / 2;
      uint64_t candidate = current->interfaces[middle].interface_id;
      if (candidate < interfaceID)
        begin = middle + 1;
      else
        end = middle;
    }
    if (begin != current->interface_count &&
        current->interfaces[begin].interface_id == interfaceID) {
      interface = &current->interfaces[begin];
      break;
    }
  }
  if (!interface || interfaceOrdinal >= interface->method_count)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t slot = interface->method_slots[interfaceOrdinal];
  if (slot == UINT32_MAX || slot >= descriptor->method_count)
    return OBELISK_RT_INVALID_HANDLE;
  const obelisk_rt_method_descriptor_v1 *method = &descriptor->methods[slot];
  if (method->signature_id != signatureID)
    return OBELISK_RT_LAYOUT_MISMATCH;
  *outMethod = method;
  return OBELISK_RT_OK;
}

static obelisk_rt_status validateMethodInvocation(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argumentCount) {
  if (!lane || !lane->heap || !lane->heap->activeOwner(lane) ||
      (!arguments && argumentCount != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  ObjectMetadata *metadata = metadataFor(receiver);
  return metadata && metadata->heap == lane->heap ? OBELISK_RT_OK
                                                  : OBELISK_RT_INVALID_HANDLE;
}

static obelisk_rt_status invokeResolvedMethod(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    const obelisk_rt_method_descriptor_v1 *method,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argumentCount,
    void *result, uint64_t resultSize) {
  if ((method->flags & OBELISK_RT_METHOD_TASK) != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!method->native_entry)
    return OBELISK_RT_TIER_UNAVAILABLE;
  // A foreign caller is not required to have placed the receiver in its own
  // root set. Keep it alive across allocations made by the method body.
  obelisk_rt_gc_root_v1 receiverRoot{};
  obelisk_rt_status status =
      lane->heap->pushRoot(lane, &receiverRoot, &receiver);
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

extern "C" obelisk_rt_status obelisk_rt_v1_method_invoke(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signatureID, const obelisk_rt_method_argument_v1 *arguments,
    uint32_t argumentCount, void *result, uint64_t resultSize) {
  obelisk_rt_status status =
      validateMethodInvocation(lane, receiver, arguments, argumentCount);
  if (status != OBELISK_RT_OK)
    return status;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  status = obelisk_rt_v1_method_resolve(receiver, slot, signatureID, &method);
  if (status != OBELISK_RT_OK)
    return status;
  return invokeResolvedMethod(lane, receiver, method, arguments, argumentCount,
                              result, resultSize);
}

extern "C" obelisk_rt_status obelisk_rt_v1_interface_method_invoke(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    uint64_t interfaceID, uint64_t interfaceOrdinal, uint64_t signatureID,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argumentCount,
    void *result, uint64_t resultSize) {
  obelisk_rt_status status =
      validateMethodInvocation(lane, receiver, arguments, argumentCount);
  if (status != OBELISK_RT_OK)
    return status;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  status = obelisk_rt_v1_interface_method_resolve(
      receiver, interfaceID, interfaceOrdinal, signatureID, &method);
  if (status != OBELISK_RT_OK)
    return status;
  return invokeResolvedMethod(lane, receiver, method, arguments, argumentCount,
                              result, resultSize);
}

static obelisk_rt_status
activateResolvedMethod(obelisk_rt_gc_lane_v1 *lane,
                       obelisk_rt_object_v1 *receiver,
                       const obelisk_rt_method_descriptor_v1 *method,
                       const obelisk_rt_method_argument_v1 *arguments,
                       uint32_t argumentCount, uint64_t *outActivation) {
  if ((method->flags & OBELISK_RT_METHOD_TASK) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  if ((method->flags & OBELISK_RT_METHOD_PURE) != 0 || !method->native_entry)
    return OBELISK_RT_TIER_UNAVAILABLE;
  obelisk_rt_gc_root_v1 receiverRoot{};
  obelisk_rt_status status =
      lane->heap->pushRoot(lane, &receiverRoot, &receiver);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_status callStatus = OBELISK_RT_OK;
  try {
    callStatus = method->native_entry(lane->context, lane, receiver, arguments,
                                      argumentCount, outActivation,
                                      sizeof(*outActivation));
    if (callStatus == OBELISK_RT_OK && *outActivation == 0)
      callStatus = OBELISK_RT_INVALID_HANDLE;
  } catch (const std::bad_alloc &) {
    callStatus = OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    callStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
  obelisk_rt_status popStatus = lane->heap->popRoot(lane, &receiverRoot);
  if (callStatus != OBELISK_RT_OK)
    return callStatus;
  // Once the thunk returns an activation, its frame owns the transferred
  // arguments. Preserve that ownership handoff even if root-stack cleanup
  // reports an internal error; returning failure would make the caller roll
  // back references already installed in the activation.
  if (popStatus != OBELISK_RT_OK)
    obelisk_rt_v1_scheduler_fail(lane->context, popStatus);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_method_task_activate(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signatureID, const obelisk_rt_method_argument_v1 *arguments,
    uint32_t argumentCount, uint64_t *outActivation) {
  if (outActivation)
    *outActivation = 0;
  if (!outActivation)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status status =
      validateMethodInvocation(lane, receiver, arguments, argumentCount);
  if (status != OBELISK_RT_OK)
    return status;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  status = obelisk_rt_v1_method_resolve(receiver, slot, signatureID, &method);
  if (status != OBELISK_RT_OK)
    return status;
  return activateResolvedMethod(lane, receiver, method, arguments,
                                argumentCount, outActivation);
}

extern "C" obelisk_rt_status obelisk_rt_v1_interface_method_task_activate(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    uint64_t interfaceID, uint64_t interfaceOrdinal, uint64_t signatureID,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argumentCount,
    uint64_t *outActivation) {
  if (outActivation)
    *outActivation = 0;
  if (!outActivation)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status status =
      validateMethodInvocation(lane, receiver, arguments, argumentCount);
  if (status != OBELISK_RT_OK)
    return status;
  const obelisk_rt_method_descriptor_v1 *method = nullptr;
  status = obelisk_rt_v1_interface_method_resolve(
      receiver, interfaceID, interfaceOrdinal, signatureID, &method);
  if (status != OBELISK_RT_OK)
    return status;
  return activateResolvedMethod(lane, receiver, method, arguments,
                                argumentCount, outActivation);
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
