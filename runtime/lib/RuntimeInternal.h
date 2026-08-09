//===- RuntimeInternal.h - Shared native runtime internals -------*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
#define OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H

#include "DesignBytecodeImage.h"
#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

constexpr uint64_t OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG = UINT64_C(1) << 63;

struct SignalWaitLatch {
  bool triggered = false;
  bool affected = false;
};

struct SignalSubscription;

struct FileEntry {
  FILE *stream = nullptr;
  int lastError = 0;
  bool writable = false;
};

struct OwnedElementTypeDescriptor {
  obelisk_rt_element_type_v1 descriptor{};
  obelisk_rt_trace_layout_v1 trace{};
  std::vector<obelisk_rt_trace_entry_v1> entries;
};

// Append an IEEE-style recursive assignment-pattern representation of a
// managed sequential container. This is shared by display formatting while
// keeping the container storage layout private to Containers.cpp.
obelisk_rt_status obelisk_rt_container_pattern(obelisk_rt_object_v1 *container,
                                               std::string &output,
                                               unsigned depth);

// Exact membership set optimized for monotonically allocated process tokens.
// Completed tokens normally form long contiguous runs, so retaining their
// await/join semantics costs one pair of endpoints per run rather than one
// hash-table node per process.
class TerminatedTokenSet {
public:
  struct InsertResult {
    bool second;
  };

  InsertResult insert(uint64_t token) {
    auto next = std::lower_bound(
        ranges.begin(), ranges.end(), token,
        [](const auto &range, uint64_t value) { return range.second < value; });
    if (next != ranges.end() && next->first <= token)
      return {false};

    bool joinsPrevious = next != ranges.begin() &&
                         (next - 1)->second != UINT64_MAX &&
                         (next - 1)->second + 1 == token;
    bool joinsNext =
        next != ranges.end() && token != UINT64_MAX && next->first == token + 1;
    if (joinsPrevious) {
      auto previous = next - 1;
      previous->second = token;
      if (joinsNext) {
        previous->second = next->second;
        ranges.erase(next);
      }
      return {true};
    }
    if (joinsNext) {
      next->first = token;
      return {true};
    }
    ranges.insert(next, {token, token});
    return {true};
  }

  size_t erase(uint64_t token) {
    auto found = std::lower_bound(
        ranges.begin(), ranges.end(), token,
        [](const auto &range, uint64_t value) { return range.second < value; });
    if (found == ranges.end() || found->first > token)
      return 0;
    if (found->first == token && found->second == token) {
      ranges.erase(found);
      return 1;
    }
    if (found->first == token) {
      ++found->first;
      return 1;
    }
    if (found->second == token) {
      --found->second;
      return 1;
    }

    size_t index = static_cast<size_t>(found - ranges.begin());
    uint64_t upper = found->second;
    ranges.insert(found + 1, {token + 1, upper});
    ranges[index].second = token - 1;
    return 1;
  }

  size_t count(uint64_t token) const {
    auto found = std::lower_bound(
        ranges.begin(), ranges.end(), token,
        [](const auto &range, uint64_t value) { return range.second < value; });
    return found != ranges.end() && found->first <= token ? 1 : 0;
  }

  // The callers reserve once before transactional batches. One new range per
  // token is the worst case and also leaves enough room for rollback splits.
  void reserveRanges(size_t rangeCount) { ranges.reserve(rangeCount); }
  size_t rangeCount() const { return ranges.size(); }
  bool empty() const { return ranges.empty(); }

private:
  std::vector<std::pair<uint64_t, uint64_t>> ranges;
};

// Small persistent design frames are recycled within a simulation context.
// Independent size buckets avoid a single allocator lock when the dynamic
// frontier is driven by multiple worker lanes. Atomic aggregate bounds ensure
// that a transient concurrency spike cannot become a second memory leak.
class ReusableByteBufferPool {
public:
  std::vector<uint8_t> acquire(size_t size) {
    std::vector<uint8_t> result;
    for (size_t index = bucketIndex(size); index != buckets.size(); ++index) {
      Bucket &bucket = buckets[index];
      std::lock_guard<std::mutex> lock(bucket.mutex);
      auto best = bucket.buffers.end();
      for (auto current = bucket.buffers.begin();
           current != bucket.buffers.end(); ++current) {
        if (current->capacity() < size)
          continue;
        if (best == bucket.buffers.end() ||
            current->capacity() < best->capacity())
          best = current;
      }
      if (best == bucket.buffers.end())
        continue;

      size_t capacity = best->capacity();
      result = std::move(*best);
      if (best != bucket.buffers.end() - 1)
        *best = std::move(bucket.buffers.back());
      bucket.buffers.pop_back();
      cachedBytes.fetch_sub(capacity, std::memory_order_relaxed);
      cachedBufferCount.fetch_sub(1, std::memory_order_relaxed);
      break;
    }
    if (result.capacity() < size)
      return std::vector<uint8_t>(size);
    result.assign(size, 0);
    return result;
  }

  void release(std::vector<uint8_t> buffer) noexcept {
    size_t capacity = buffer.capacity();
    if (capacity == 0 || capacity > kMaxBufferBytes)
      return;
    buffer.clear();

    size_t count = cachedBufferCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= kMaxBuffers) {
      cachedBufferCount.fetch_sub(1, std::memory_order_relaxed);
      return;
    }
    size_t bytes = cachedBytes.load(std::memory_order_relaxed);
    for (;;) {
      if (capacity > kMaxCachedBytes - bytes) {
        cachedBufferCount.fetch_sub(1, std::memory_order_relaxed);
        return;
      }
      if (cachedBytes.compare_exchange_weak(bytes, bytes + capacity,
                                            std::memory_order_relaxed))
        break;
    }

    try {
      Bucket &bucket = buckets[bucketIndex(capacity)];
      std::lock_guard<std::mutex> lock(bucket.mutex);
      bucket.buffers.push_back(std::move(buffer));
    } catch (...) {
      cachedBytes.fetch_sub(capacity, std::memory_order_relaxed);
      cachedBufferCount.fetch_sub(1, std::memory_order_relaxed);
      // Recycling is an optimization; the buffer is freed by its destructor.
    }
  }

  size_t size() const {
    return cachedBufferCount.load(std::memory_order_relaxed);
  }
  size_t byteSize() const {
    return cachedBytes.load(std::memory_order_relaxed);
  }

private:
  static constexpr unsigned kMinSizeShift = 6;
  static constexpr unsigned kMaxSizeShift = 20;
  static constexpr size_t kBucketCount = kMaxSizeShift - kMinSizeShift + 1;
  static constexpr size_t kMaxBuffers = 64;
  static constexpr size_t kMaxBufferBytes = size_t{1} << kMaxSizeShift;
  static constexpr size_t kMaxCachedBytes = 16 * 1024 * 1024;

  struct alignas(64) Bucket {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> buffers;
  };

  static size_t bucketIndex(size_t size) {
    size_t classSize = size_t{1} << kMinSizeShift;
    size_t index = 0;
    while (classSize < size && index + 1 != kBucketCount) {
      classSize <<= 1;
      ++index;
    }
    return index;
  }

  std::array<Bucket, kBucketCount> buckets;
  std::atomic<size_t> cachedBytes{0};
  std::atomic<size_t> cachedBufferCount{0};
};

inline bool obelisk_rt_is_process_home_region(uint32_t region) {
  return region == OBELISK_RT_REGION_ACTIVE ||
         region == OBELISK_RT_REGION_OBSERVED ||
         region == OBELISK_RT_REGION_REACTIVE ||
         region == OBELISK_RT_REGION_POSTPONED;
}

inline bool obelisk_rt_is_mutable_home_region(uint32_t region) {
  return region == OBELISK_RT_REGION_ACTIVE ||
         region == OBELISK_RT_REGION_REACTIVE;
}

inline bool obelisk_rt_decode_schedule_flags(uint32_t flags, uint32_t &phase,
                                             uint32_t &homeRegion) {
  constexpr uint32_t known =
      OBELISK_RT_SCHEDULE_FINAL | OBELISK_RT_SCHEDULE_HOME_MASK |
      OBELISK_RT_SCHEDULE_INITIAL;
  if ((flags & ~known) != 0)
    return false;
  phase = (flags & OBELISK_RT_SCHEDULE_FINAL) != 0 ? 1u : 0u;
  homeRegion =
      (flags & OBELISK_RT_SCHEDULE_HOME_MASK) >> OBELISK_RT_SCHEDULE_HOME_SHIFT;
  return obelisk_rt_is_process_home_region(homeRegion) &&
         (phase == 0 || homeRegion == OBELISK_RT_REGION_ACTIVE);
}

inline bool obelisk_rt_next_queued_region(uint32_t homeRegion,
                                          uint32_t suspendKind,
                                          uint64_t delayPayload,
                                          uint32_t actionFlags,
                                          uint32_t &queuedRegion) {
  constexpr uint32_t known = OBELISK_RT_ACTION_FRAME_WAIT_RECORD |
                             OBELISK_RT_ACTION_RESUME_REGION_VALID |
                             OBELISK_RT_ACTION_RESUME_REGION_MASK;
  if ((actionFlags & ~known) != 0)
    return false;
  if ((actionFlags & OBELISK_RT_ACTION_RESUME_REGION_VALID) != 0) {
    queuedRegion = (actionFlags & OBELISK_RT_ACTION_RESUME_REGION_MASK) >>
                   OBELISK_RT_ACTION_RESUME_REGION_SHIFT;
    return obelisk_rt_is_process_home_region(queuedRegion);
  }
  if ((actionFlags & OBELISK_RT_ACTION_RESUME_REGION_MASK) != 0)
    return false;
  if (suspendKind == OBELISK_RT_SUSPEND_DELAY && delayPayload == 0) {
    if (!obelisk_rt_is_mutable_home_region(homeRegion))
      return false;
    queuedRegion = homeRegion + 1;
    return true;
  }
  queuedRegion = homeRegion;
  return true;
}

inline uint32_t obelisk_rt_commit_region(uint32_t homeRegion) {
  return obelisk_rt_is_mutable_home_region(homeRegion) ? homeRegion + 2
                                                       : UINT32_MAX;
}

struct ScheduledProcess {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  std::vector<obelisk_rt_process_instance_v1 *> callers;
  std::vector<uint64_t> controls;
  uint64_t token = 0;
  uint64_t parent = 0;
  uint64_t observedEpoch = 0;
  uint64_t wakeTime = 0;
  uint64_t waitOffset = 0;
  uint64_t waitSize = 0;
  std::vector<uint64_t> waitGenerations;
  std::vector<std::unique_ptr<SignalSubscription>> signalSubscriptions;
  std::unique_ptr<SignalWaitLatch> signalLatch;
  std::vector<std::pair<uint32_t, uint32_t>> continuationRanks;
  // AOT-owned actors may execute selected continuation activations through
  // bytecode without leaving the static schedule. The sorted table includes
  // continuation zero when the initial activation is bytecode-only.
  std::vector<uint32_t> bytecodeContinuations;
  uint32_t aotActorSlot = UINT32_MAX;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  uint32_t scheduleRank = UINT32_MAX;
  uint64_t insertionSequence = 0;
  obelisk_rt_random_state_v1 random{};
  // Executable event-region ordinal. Normally the immutable home region,
  // temporarily an inactive region after #0 or an explicit resume override.
  uint32_t queuedRegion = 0;
  bool started = false;
  bool urgent = false;
  bool signalTriggered = false;
  bool initialProcess = false;
};

struct SignalValueSnapshot {
  uint64_t sequence = 0;
  bool value = false;
  bool unknown = false;
};

struct NativeAutomaticState {
  uint64_t bitWidth = 0;
  obelisk_rt_process_instance_v1 *owner = nullptr;
  uint64_t designOwner = 0;
  uint64_t referenceCount = 1;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  obelisk_rt_object_v1 *managedValue = nullptr;
  bool managedRootRegistered = false;
  std::vector<uint64_t> managedRootByteOffsets;
};

struct EventState {
  uint64_t generation = 0;
  uint64_t lastTriggeredTime = 0;
};

struct NativeStaticState {
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
};

struct NativeStaticStateRange {
  uint64_t bitOffset = 0;
  uint64_t bitEnd = 0;
  uint64_t prefixEnd = 0;
  uint32_t id = 0;
};

struct ScheduledNBA {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  uint32_t retainedAutomaticID = 0;
  uint8_t *valuePlane = nullptr;
  uint8_t *unknownPlane = nullptr;
  uint64_t planeBitCount = 0;
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  bool stringValue = false;
  bool managedValue = false;
  bool inlinePacked = false;
  obelisk_rt_string_v1 rootedString = 0;
  obelisk_rt_object_v1 *rootedManaged = nullptr;
  uint64_t inlineValue = 0;
  uint64_t inlineUnknown = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

struct StaticNBAAccumulator {
  uint8_t *valuePlane = nullptr;
  uint8_t *unknownPlane = nullptr;
  uint64_t planeBitCount = 0;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  uint64_t sequence = 0;
  bool valid = false;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  std::vector<uint64_t> writeMask;
  std::vector<uint64_t> changed;
  std::vector<uint64_t> posedge;
  std::vector<uint64_t> negedge;
};

struct ScheduledManagedNBA {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  obelisk_rt_object_v1 *destination = nullptr;
  uint64_t offset = 0;
  uint64_t planeSize = 0;
  bool referencePath = false;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  std::vector<obelisk_rt_object_v1 *> managedValues;
};

struct ScheduledDesignNBA {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  uint32_t handleKind = 0;
  int64_t begin = 0;
  int64_t start = 0;
  int64_t end = 0;
  uint64_t bitWidth = 0;
  bool stringValue = false;
  obelisk_rt_string_v1 rootedString = 0;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
};

struct ScheduledDesignEvent {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  uint64_t stableID = 0;
  uint32_t retainedAutomaticID = 0;
};

struct DesignActivation {
  uint32_t function = 0;
  uint32_t continuation = 0;
  std::vector<uint8_t> frame;
  uint64_t scratchOffset = 0;
  uint64_t scratchSize = 0;
  uint32_t scheduleRank = UINT32_MAX;
};

struct ScheduledDesignTask {
  uint64_t id = 0;
  uint64_t parent = 0;
  uint32_t function = 0;
  uint32_t continuation = 0;
  std::vector<DesignActivation> callers;
  std::vector<uint64_t> controls;
  std::vector<uint8_t> frame;
  uint64_t scratchOffset = 0;
  uint64_t scratchSize = 0;
  uint64_t observedEpoch = 0;
  uint64_t wakeTime = 0;
  uint64_t waitOffset = 0;
  uint64_t waitSize = 0;
  std::vector<uint64_t> waitGenerations;
  std::vector<std::unique_ptr<SignalSubscription>> signalSubscriptions;
  std::unique_ptr<SignalWaitLatch> signalLatch;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  uint32_t scheduleRank = UINT32_MAX;
  uint32_t queuedRegion = 0;
  uint64_t insertionSequence = 0;
  obelisk_rt_random_state_v1 random{};
  bool started = false;
  bool urgent = false;
  bool terminated = false;
  bool signalTriggered = false;
};

struct SignalSubscriptionBucketKey {
  uint32_t kind = 0;
  uint32_t id = 0;
  int64_t page = 0;

  bool operator==(const SignalSubscriptionBucketKey &other) const {
    return kind == other.kind && id == other.id && page == other.page;
  }
};

struct SignalSubscriptionBucketKeyHash {
  size_t operator()(const SignalSubscriptionBucketKey &key) const {
    size_t hash =
        std::hash<uint64_t>{}((uint64_t{key.kind} << 32) | uint64_t{key.id});
    size_t page = std::hash<int64_t>{}(key.page);
    return hash ^ (page + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2));
  }
};

struct SignalSubscriptionBucketSlot {
  SignalSubscriptionBucketKey key;
  size_t bucketIndex = 0;
};

struct SignalSubscriptionBucketEntry {
  SignalSubscription *subscription = nullptr;
  size_t slotIndex = 0;
};

struct SignalSubscription {
  enum Target : uint8_t {
    NativeDirectWait,
    DesignDirectWait,
    NativeComputedWait,
    DesignComputedWait,
  };

  uint64_t stableID = 0;
  uint64_t bitWidth = 0;
  uint64_t lastExaminedSequence = 0;
  uint64_t waiterToken = 0;
  uint32_t edge = 0;
  Target target = NativeDirectWait;
  SignalWaitLatch *latch = nullptr;
  std::vector<SignalSubscriptionBucketSlot> bucketSlots;
};

struct SignalSubscriptionDiagnostics {
  uint64_t publications = 0;
  uint64_t subscriptionsCurrent = 0;
  uint64_t subscriptionsHighWater = 0;
  uint64_t subscribersExamined = 0;
  uint64_t readinessCalls = 0;
  uint64_t candidateScans = 0;
  uint64_t schedulerIterations = 0;
  uint64_t fallbackRescans = 0;
  uint64_t aotNodeExecutions = 0;
  uint64_t aotActorExecutions[64] = {};
  uint64_t aotRegionPasses = 0;
  uint64_t aotFanoutEntries = 0;
  uint64_t aotNBAStages = 0;
  uint64_t aotNBACommits = 0;
  uint64_t aotStateFastPaths = 0;
  uint64_t aotStateSlowPaths = 0;
  uint64_t aotDeadlineHighWater = 0;
  uint64_t aotFallbacks = 0;
};

// Three packed edge planes with allocation-free storage for common signals up
// to 256 bits. Keeping edge identity per bit lets range publication batch map
// lookups without conflating a posedge on one bit with a negedge on another.
class PackedSignalTransitionBuffer {
public:
  explicit PackedSignalTransitionBuffer(uint64_t bitWidth)
      : byteCount((bitWidth + 7) / 8) {
    if (bitWidth > UINT64_MAX - 7 ||
        byteCount > std::numeric_limits<size_t>::max() / uint64_t{3})
      throw std::bad_alloc();
    if (byteCount > kInlineBytes)
      overflow.assign(static_cast<size_t>(byteCount * 3), 0);
  }

  void record(uint64_t bit, uint32_t edges) {
    set(changed(), bit);
    if ((edges & OBELISK_RT_SIGNAL_POSEDGE) != 0)
      set(posedge(), bit);
    if ((edges & OBELISK_RT_SIGNAL_NEGEDGE) != 0)
      set(negedge(), bit);
  }

  uint8_t *changed() { return storage(); }
  uint8_t *posedge() { return storage() + byteCount; }
  uint8_t *negedge() { return storage() + byteCount * 2; }

private:
  static constexpr uint64_t kInlineBytes = 32;

  static void set(uint8_t *plane, uint64_t bit) {
    plane[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
  }
  uint8_t *storage() {
    return byteCount <= kInlineBytes ? inlineStorage.data() : overflow.data();
  }

  uint64_t byteCount;
  std::array<uint8_t, kInlineBytes * 3> inlineStorage{};
  std::vector<uint8_t> overflow;
};

struct ImportBinding {
  obelisk_rt_import_callback_v1 callback = nullptr;
  void *userData = nullptr;
  uint64_t abiSignature = 0;
};

struct ControlActivation {
  uint64_t target = 0;
  uint64_t memberships = 0;
};

struct DpiScopeHandle {
  obelisk_rt_context *context = nullptr;
  uint64_t id = 0;
  uint64_t parentID = UINT64_MAX;
  std::string name;
  int32_t timeUnit = 0;
  int32_t timePrecision = 0;
  std::unordered_map<void *, void *> userData;
};

class ManagedHeap;

struct NetAliasRange {
  uint64_t valueOffset = 0;
  uint64_t targetOffset = 0;
  uint64_t width = 0;
  bool fourState = false;
};

struct NetAliasCache {
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  std::unordered_map<uint64_t, uint64_t> rootByBit;
  std::unordered_map<uint64_t, std::vector<uint64_t>> members;
  std::unordered_map<uint64_t, std::vector<uint64_t>> driverBits;
  std::vector<NetAliasRange> nets;
  std::vector<NetAliasRange> drivers;
};

// Decoded view of the immutable reflection image. Context creation validates
// the complete image before publishing this cache; context-owned consumers can
// therefore perform constant-time structural checks without re-checksumming or
// re-walking the design on every query.
struct DesignDatabaseCache {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  uint32_t profile = 0;
  uint64_t root = 0;
  uint64_t scopes = 0;
  uint64_t scopeCount = 0;
  uint64_t objects = 0;
  uint64_t objectCount = 0;
  uint64_t types = 0;
  uint64_t typeCount = 0;
  uint64_t strings = 0;
  uint64_t stringSize = 0;
  uint64_t index = 0;
  uint64_t indexCount = 0;
  uint64_t stateBitCount = 0;
  bool validated = false;
};

struct CoverageTypeState {
  std::vector<uint32_t> coverpointBins;
  std::vector<uint64_t> instances;
};

struct CoverageInstanceState {
  uint64_t typeID = 0;
  bool enabled = true;
  std::vector<std::vector<uint64_t>> hits;
};

struct obelisk_rt_context {
  // Mutable state is guarded separately from logical execution. Evaluator
  // callbacks release `mutex` while arbitrary user code runs, but retain the
  // recursive transaction lock so another thread cannot interleave a state or
  // scheduler mutation. Nested calls on the evaluator thread re-enter both.
  std::recursive_mutex mutex;
  std::recursive_mutex transactionMutex;
  std::thread::id transactionOwner;
  uint32_t transactionDepth = 0;
  bool destroyPending = false;
  std::array<FileEntry, 31> mcd;
  // 0x80000000, 0x80000001, and 0x80000002 are the IEEE predefined stdin,
  // stdout, and stderr descriptors. Dynamic descriptors begin at index 3.
  std::vector<FileEntry> files;
  std::vector<uint32_t> freeFiles;
  std::vector<uint32_t> freeMCDs;
  // Command-line arguments introduced by '+', stored without that prefix in
  // the order they were given. $test$plusargs and $value$plusargs match
  // against these.
  std::vector<std::string> plusargs;
  // $timeformat override for %t. Until one is executed the format env's own
  // width, multiplier, and suffix govern, which is the design-precision
  // integer form IEEE specifies as the default.
  struct TimeFormatState {
    bool active = false;
    // Decimal exponent, in seconds, of the unit %t reports in.
    int32_t units = 0;
    uint32_t fractionDigits = 0;
    uint32_t width = 20;
    std::string suffix;
  } timeFormat;
  std::shared_ptr<const uint8_t> errorLifetime;
  std::vector<ScheduledProcess> scheduledProcesses;
  const obelisk_rt_native_schedule_plan *nativeSchedulePlan = nullptr;
  const obelisk_rt_static_nba_root *nativeScheduleNBARoots = nullptr;
  uint32_t nativeScheduleNBARootCount = 0;
  const obelisk_rt_static_nba_site *nativeScheduleNBASites = nullptr;
  uint64_t nativeScheduleNBASiteCount = 0;
  const obelisk_rt_static_fanout_entry *nativeScheduleFanoutEntries = nullptr;
  uint64_t nativeScheduleFanoutEntryCount = 0;
  const obelisk_rt_static_actor_root *nativeScheduleActorRoots = nullptr;
  uint64_t nativeScheduleActorRootCount = 0;
  std::vector<std::pair<uint64_t, uint64_t>> nativeScheduleActorRootRanges;
  std::vector<uint32_t> nativeScheduleNBASiteIndex;
  std::vector<obelisk_rt_process_instance_v1 *> nativeScheduleActors;
  std::vector<uint64_t> nativeScheduleActorTokens;
  std::vector<size_t> nativeScheduleActorIndices;
  std::vector<obelisk_rt_native_schedule_node> nativeScheduleNodes;
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>>
      nativeScheduleActorNodes;
  std::vector<uint64_t> nativeScheduleReadyNodes;
  std::vector<uint32_t> nativeScheduleFanoutNodes;
  std::vector<std::pair<uint64_t, uint64_t>> nativeScheduleFanoutRanges;
  uint32_t nativeScheduleMinimumActivatedNode = UINT32_MAX;
  bool nativeScheduleClockIngressPending = false;
  uint32_t nativeScheduleDirectActorSlot = UINT32_MAX;
  uint32_t nativeScheduleCheckpointActorSlot = UINT32_MAX;
  uint32_t nativeScheduleCheckpointContinuation = 0;
  obelisk_rt_native_checkpoint_callback nativeScheduleCheckpointCallback =
      nullptr;
  std::vector<uint64_t> nativeScheduleDeadlines;
  std::vector<uint32_t> nativeScheduleDeadlineHeap;
  std::vector<uint32_t> nativeScheduleDeadlinePositions;
  std::vector<obelisk_rt_aot_deopt_actor> nativeScheduleSnapshotActors;
  std::vector<obelisk_rt_aot_deopt_nba> nativeScheduleSnapshotNBAs;
  bool nativeScheduleRunning = false;
  bool nativeScheduleDeoptimized = false;
  bool nativeScheduleExternalWritePending = false;
  std::unordered_set<uint32_t> nativeScheduleTransientDirtyRoots;
  std::unordered_set<uint32_t> nativeSchedulePersistentDirtyRoots;
  std::vector<uint64_t> nativeScheduleTransientDirtyMask;
  std::vector<uint64_t> nativeSchedulePersistentDirtyMask;
  std::vector<uint64_t> nativeScheduleTransientDirtySummary;
  std::vector<uint64_t> nativeSchedulePersistentDirtySummary;
  bool nativeScheduleDirtyRootsPresent = false;
  bool nativeScheduleAVX2 = false;
  bool nativeScheduleGuardedFanoutActive = false;
  uint32_t nativeScheduleForcedSlot = UINT32_MAX;
  bool nativeScheduleSingleStep = false;
  bool nativeScheduleForcedExecuted = false;
  bool nativeScheduleControlOnly = false;
  bool nativeScheduleProcessFilterActive = false;
  uint64_t nativeScheduleForcedProcessToken = 0;
  bool nativeScheduleStopAtCleanBoundary = false;
  bool nativeScheduleCleanBoundaryReached = false;
  bool nativeScheduleDesignTaskFilterActive = false;
  uint64_t nativeScheduleForcedDesignTask = 0;
  uint64_t nativePeriodicRuntimeDeadline = UINT64_MAX;
  std::vector<uint32_t> nativePeriodicClockActorSlots;
  std::unordered_map<uint64_t, size_t> scheduledProcessIndices;
  std::unordered_set<uint64_t> nativePollCandidates;
  // Lazy min-heap of (wake time, process token). Stale entries are discarded
  // when queried after a process resumes, changes wait kind, or terminates.
  std::vector<std::pair<uint64_t, uint64_t>> scheduledProcessDelayHeap;
  bool scheduledFinalProcessPresent = false;
  std::unordered_map<uint64_t, SignalValueSnapshot> signalValueSnapshots;
  std::unordered_map<SignalSubscriptionBucketKey,
                     std::vector<SignalSubscriptionBucketEntry>,
                     SignalSubscriptionBucketKeyHash>
      signalSubscriptionBuckets;
  std::vector<uint64_t> pendingNativeComputedWaiters;
  std::vector<uint64_t> pendingDesignComputedWaiters;
  std::unordered_set<uint64_t> nativeConditionalSignalWaiters;
  std::unordered_set<uint64_t> designConditionalSignalWaiters;
  uint64_t schedulerSelectionGeneration = 1;
  bool signalDiagnosticsEnabled = false;
  bool signalDiagnosticsReport = false;
  SignalSubscriptionDiagnostics signalDiagnostics;
  std::vector<ScheduledNBA> scheduledNBAs;
  std::vector<StaticNBAAccumulator> staticNBAAccumulators;
  bool staticNBAAccumulatorsPending = false;
  std::vector<uint8_t> staticNBASlowRoots;
  bool staticNBASlowRootsPresent = false;
  std::vector<uint8_t> staticNBARootHasFanout;
  std::vector<uint8_t> nativeScheduleGeneratedNBAStageCounts;
  std::vector<uint64_t> nativeScheduleGeneratedNBAOffsets;
  bool nativeScheduleGeneratedBatchEligible = false;
  bool nativeScheduleHasGeneratedNBAAccumulators = false;
  std::vector<ScheduledManagedNBA> scheduledManagedNBAs;
  std::vector<ScheduledDesignNBA> scheduledDesignNBAs;
  std::vector<ScheduledDesignEvent> scheduledDesignEvents;
  std::vector<ScheduledDesignTask> scheduledDesignTasks;
  std::unordered_map<uint64_t, size_t> scheduledDesignTaskIndices;
  std::unordered_set<uint64_t> designPollCandidates;
  ReusableByteBufferPool designTaskFrames;
  uint64_t nextSchedulerSequence = 1;
  uint64_t nextNativeProcessToken = 1;
  uint32_t nextNativeAutomaticID = 1;
  uint64_t nextDesignTaskID = 1;
  uint64_t nextProcessInsertionSequence = 1;
  uint64_t activeDesignTaskID = 0;
  uint32_t activeDesignTaskPhase = 0;
  uint32_t activeHomeRegion = UINT32_MAX;
  uint32_t activeExecRegion = UINT32_MAX;
  uint64_t activeLogicalProcessToken = 0;
  // A bytecode design task is moved out of the scheduler vector while it
  // executes, so its lane-local stream cannot be rediscovered by token.
  obelisk_rt_random_state_v1 *activeRandom = nullptr;
  uint64_t monitorLogicalProcessToken = 0;
  bool monitorEnabled = true;
  std::vector<uint64_t> activeControls;
  obelisk_rt_process_instance_v1 *activeNativeProcess = nullptr;
  bool designTaskExecuting = false;
  bool schedulerCompactionPending = false;
  size_t schedulerDeadProcessCount = 0;
  size_t schedulerDeadDesignTaskCount = 0;
  TerminatedTokenSet terminatedDesignTasks;
  TerminatedTokenSet terminatedNativeProcesses;
  uint64_t nextControlActivation = 1;
  std::unordered_map<uint64_t, ControlActivation> controlActivations;
  std::unordered_set<uint64_t> initializedStaticSites;
  uint64_t deferredImmediateTime = UINT64_MAX;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      deferredImmediateSites;
  std::unordered_map<uint32_t, NativeStaticState> nativeStaticStates;
  // Lazily sorted interval index for reflection/VPI range lookups.
  mutable std::vector<NativeStaticStateRange> nativeStaticStateRanges;
  mutable bool nativeStaticStateRangesValid = false;
  std::vector<NativeStaticState> nativeScheduleStaticStateIndex;
  std::vector<uint8_t> nativeScheduleStaticStateFanoutEdges;
  std::unordered_map<uint32_t, NativeAutomaticState> nativeAutomaticStates;
  std::map<uint64_t, EventState> events;
  std::unordered_map<uint32_t, ImportBinding> imports;
  std::vector<std::unique_ptr<DpiScopeHandle>> dpiScopes;
  std::unordered_map<std::string, DpiScopeHandle *> dpiScopesByName;
  size_t schedulerCursor = 0;
  uint64_t schedulerEpoch = 1;
  uint64_t schedulerTime = 0;
  uint64_t schedulerPreponedTime = UINT64_MAX;
  uint64_t schedulerSlotProgress = 0;
  bool schedulerRunningFinals = false;
  bool schedulerFinishRequested = false;
  // Mirrored as a full word for generated code. The address is handed out
  // only while the native scheduler owns the context transaction.
  uint32_t nativePeriodicTerminationRequested = 0;
  uint32_t schedulerFinishVerbosity = 0;
  obelisk_rt_status schedulerFinishStatus = OBELISK_RT_OK;
  obelisk_rt_status schedulerStatus = OBELISK_RT_OK;
  uint32_t observerDepth = 0;
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  // Live simulation state is owned by the context.  The planes use the same
  // little-endian limb representation as bytecode values and are never stored
  // in the immutable reflection image.
  std::vector<uint64_t> stateValue;
  std::vector<uint64_t> stateUnknown;
  // Language and VPI force state is allocated on first use. A set bit masks
  // every ordinary publication to the corresponding canonical design bit.
  std::vector<uint64_t> forceMask;
  std::vector<uint64_t> assignMask;
  std::vector<uint64_t> assignValue;
  std::vector<uint64_t> assignUnknown;
  // Built once from the immutable execution image and shared by net
  // resolution, force/release, deposits, and reflection connectivity checks.
  NetAliasCache netAliases;
  DesignDatabaseCache designDatabase;
  bool designDatabaseRegistered = false;
  obelisk::designbytecode::Image designBytecodeImage;
  bool designBytecodeImageValidated = false;
  void *vpiState = nullptr;
  std::unordered_map<uint64_t, const obelisk_rt_class_descriptor_v1 *>
      managedClasses;
  std::unordered_map<uint64_t, const obelisk_rt_element_type_v1 *>
      managedElementTypes;
  std::unordered_map<uint64_t, std::unique_ptr<OwnedElementTypeDescriptor>>
      managedOwnedElementTypes;
  uint64_t nextCoverageInstance = 1;
  std::unordered_map<uint64_t, CoverageTypeState> coverageTypes;
  std::unordered_map<uint64_t, CoverageInstanceState> coverageInstances;
  std::vector<obelisk_rt_process_instance_v1 *> managedRootProcesses;
  ManagedHeap *managedHeap = nullptr;
  obelisk_rt_random_state_v1 random{};

  obelisk_rt_context();
  ~obelisk_rt_context();
};

inline bool
obelisk_rt_has_conditional_signal_waiters(const obelisk_rt_context *context) {
  return context && (!context->nativeConditionalSignalWaiters.empty() ||
                     !context->designConditionalSignalWaiters.empty());
}

inline bool
obelisk_rt_design_signal_wait_blocked(const ScheduledDesignTask &task) {
  if (task.terminated || !task.started || task.signalTriggered)
    return false;
  bool signalSuspend = task.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
                       task.suspendKind == OBELISK_RT_SUSPEND_EDGE;
  if (signalSuspend && task.waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
      task.waitOffset <= task.frame.size() &&
      task.waitSize <= task.frame.size() - task.waitOffset) {
    const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    if (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
        wait->flags == OBELISK_RT_WAIT_EDGE_IFF)
      return true;
  }
  return !task.signalSubscriptions.empty() && task.signalLatch &&
         !task.signalLatch->triggered &&
         (signalSuspend || task.suspendKind == OBELISK_RT_SUSPEND_OBSERVER);
}

ManagedHeap *obelisk_rt_managed_heap_create(obelisk_rt_context *context);
void obelisk_rt_managed_heap_destroy(ManagedHeap *heap) noexcept;
obelisk_rt_status
obelisk_rt_managed_execution_enter(obelisk_rt_context *context,
                                   obelisk_rt_gc_lane_v1 **outLane,
                                   bool *outEntered);
void obelisk_rt_managed_execution_leave(obelisk_rt_gc_lane_v1 *lane,
                                        bool entered);
using ManagedRootVisit = void (*)(void *, obelisk_rt_object_v1 **);
using ManagedRootEnumerate = void (*)(void *, ManagedRootVisit, void *);

// Caller-owned lane-local root provider. Providers form a stack and enumerate
// typed storage directly, avoiding a root record or allocation per managed
// value.
struct ManagedRootProvider {
  ManagedRootEnumerate enumerate = nullptr;
  void *environment = nullptr;
  ManagedRootProvider *previous = nullptr;
  uint64_t cookie = 0;
};

obelisk_rt_status obelisk_rt_managed_roots_push(obelisk_rt_gc_lane_v1 *lane,
                                                ManagedRootProvider *provider,
                                                ManagedRootEnumerate enumerate,
                                                void *environment);
obelisk_rt_status obelisk_rt_managed_roots_pop(obelisk_rt_gc_lane_v1 *lane,
                                               ManagedRootProvider *provider);
const obelisk_rt_class_descriptor_v1 *
obelisk_rt_managed_class_lookup(obelisk_rt_context *context, uint64_t classID);
obelisk_rt_random_state_v1 *
obelisk_rt_random_active_state_unlocked(obelisk_rt_context *context);
void obelisk_rt_random_split_unlocked(obelisk_rt_context *context,
                                      obelisk_rt_random_state_v1 &child);
void obelisk_rt_random_seed_context_unlocked(obelisk_rt_context *context,
                                             uint64_t seed);
bool obelisk_rt_managed_object_belongs_to(
    obelisk_rt_context *context, obelisk_rt_object_v1 *object) noexcept;
obelisk_rt_context *
obelisk_rt_managed_lane_context(const obelisk_rt_gc_lane_v1 *lane) noexcept;
const obelisk_rt_element_type_v1 *
obelisk_rt_managed_element_type_lookup(obelisk_rt_context *context,
                                       uint64_t typeID);

using ManagedObjectAccess = obelisk_rt_status (*)(void *, uint8_t *, uint64_t);
using ManagedTraceVisit = void (*)(void *, obelisk_rt_object_v1 *);
obelisk_rt_status obelisk_rt_managed_allocate(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_managed_kind_v1 kind,
                                              uint64_t extent,
                                              uint64_t alignment,
                                              const void *runtimeDescriptor,
                                              obelisk_rt_object_v1 **outObject);
obelisk_rt_status
obelisk_rt_managed_object_access(obelisk_rt_object_v1 *object,
                                 obelisk_rt_managed_kind_v1 expectedKind,
                                 ManagedObjectAccess access, void *environment);
obelisk_rt_managed_kind_v1
obelisk_rt_managed_object_kind(const obelisk_rt_object_v1 *object) noexcept;
uint64_t
obelisk_rt_managed_object_extent(const obelisk_rt_object_v1 *object) noexcept;
obelisk_rt_context *
obelisk_rt_managed_object_context(const obelisk_rt_object_v1 *object) noexcept;
void obelisk_rt_managed_trace_runtime_object(obelisk_rt_managed_kind_v1 kind,
                                             uint8_t *object, uint64_t extent,
                                             ManagedTraceVisit visit,
                                             void *environment) noexcept;
obelisk_rt_status obelisk_rt_reference_path_shape(obelisk_rt_object_v1 *path,
                                                  uint64_t valueSize,
                                                  uint64_t bitWidth,
                                                  uint32_t fourState,
                                                  uint32_t managedValue);
void obelisk_rt_enumerate_design_managed_roots(
    obelisk_rt_context *context, ManagedRootVisit visit,
    void *visitorEnvironment) noexcept;
obelisk_rt_status
obelisk_rt_validate_string(obelisk_rt_context *context,
                           obelisk_rt_string_v1 string) noexcept;

class ManagedExecutionScope {
public:
  explicit ManagedExecutionScope(obelisk_rt_context *context)
      : status(context ? obelisk_rt_managed_execution_enter(context, &lane,
                                                            &entered)
                       : OBELISK_RT_OK) {}
  ManagedExecutionScope(const ManagedExecutionScope &) = delete;
  ManagedExecutionScope &operator=(const ManagedExecutionScope &) = delete;
  ~ManagedExecutionScope() {
    obelisk_rt_managed_execution_leave(lane, entered);
  }

  obelisk_rt_status getStatus() const { return status; }
  obelisk_rt_gc_lane_v1 *getLane() const { return lane; }

private:
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  obelisk_rt_status status = OBELISK_RT_OK;
};

// Serialize a complete external mutation or scheduler fragment across any
// recursively invoked observer callbacks. If a callback destroys its active
// context, final cleanup is deferred until the outermost transaction returns.
class ContextTransaction {
public:
  explicit ContextTransaction(obelisk_rt_context *context);
  ContextTransaction(const ContextTransaction &) = delete;
  ContextTransaction &operator=(const ContextTransaction &) = delete;
  ~ContextTransaction() noexcept;

private:
  obelisk_rt_context *context = nullptr;
  std::unique_lock<std::recursive_mutex> transactionLock;
  obelisk_rt_context *previousThreadContext = nullptr;
  uint32_t previousThreadDepth = 0;
  bool nested = false;
};

class ContextCallbackUnlock {
public:
  explicit ContextCallbackUnlock(obelisk_rt_context *context)
      : context(context) {
    context->mutex.unlock();
  }
  ContextCallbackUnlock(const ContextCallbackUnlock &) = delete;
  ContextCallbackUnlock &operator=(const ContextCallbackUnlock &) = delete;
  ~ContextCallbackUnlock() { context->mutex.lock(); }

private:
  obelisk_rt_context *context;
};

void setLastErrorUnlocked(obelisk_rt_context *context, std::string message);
void setLastError(obelisk_rt_context *context, std::string message);
void obelisk_rt_report_signal_diagnostics_unlocked(obelisk_rt_context *context);
void obelisk_rt_release_native_schedule_plan(
    obelisk_rt_context *context) noexcept;
void obelisk_rt_aot_external_write_unlocked(obelisk_rt_context *context);
void obelisk_rt_aot_external_write_range_unlocked(obelisk_rt_context *context,
                                                  uint64_t bitOffset,
                                                  uint64_t bitWidth,
                                                  bool persistent);
void obelisk_rt_aot_external_write_handle_unlocked(obelisk_rt_context *context,
                                                   uint64_t stableID,
                                                   uint64_t bitOffset,
                                                   uint64_t bitWidth,
                                                   bool persistent);
bool obelisk_rt_aot_external_deposit_unlocked(obelisk_rt_context *context,
                                              uint64_t stableID,
                                              uint64_t bitOffset,
                                              uint64_t bitWidth);
void obelisk_rt_aot_release_range_unlocked(obelisk_rt_context *context,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth);

void obelisk_rt_retain_controls_unlocked(obelisk_rt_context *context,
                                         const std::vector<uint64_t> &controls);
void obelisk_rt_release_control_unlocked(obelisk_rt_context *context,
                                         uint64_t control);
void obelisk_rt_release_controls_unlocked(
    obelisk_rt_context *context, const std::vector<uint64_t> &controls);

// A join_none branch may finish while processes spawned beneath it remain
// live. Keep those descendants reachable from the surviving process tree so
// a later wait fork or disable fork in the ancestor still sees them.
inline void obelisk_rt_reparent_process_children_unlocked(
    obelisk_rt_context *context, uint64_t parent, uint64_t replacement) {
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && process.parent == parent)
      process.parent = replacement;
  for (ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (!task.terminated && task.parent == parent)
      task.parent = replacement;
}

obelisk_rt_status obelisk_rt_initialize_design_bytecode_image(
    const obelisk_rt_execution_descriptor_v1 &execution,
    obelisk::designbytecode::Image &image) noexcept;

DpiScopeHandle *obelisk_rt_find_dpi_scope(obelisk_rt_context *context,
                                          uint64_t id);
obelisk_rt_status obelisk_rt_initialize_dpi_scopes(
    obelisk_rt_context *context,
    const obelisk_rt_execution_descriptor_v1 *execution);

template <typename Callable>
obelisk_rt_status guarded(obelisk_rt_context *context,
                          Callable &&callable) noexcept {
  try {
    return callable();
  } catch (const std::bad_alloc &) {
    setLastError(context, "runtime allocation failed");
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    setLastError(context, "unexpected runtime exception");
    return OBELISK_RT_IO_ERROR;
  }
}

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output);
bool validBytes(const void *data, uint64_t size);
std::string hostErrorMessage(int error);

obelisk_rt_status writeUnlocked(obelisk_rt_context *context,
                                uint32_t descriptor, const void *data,
                                uint64_t size, uint64_t *outWritten);

// Fully validate immutable bytecode metadata without executing or mutating a
// process frame. Missing continuations are tier-unavailable; malformed
// programs are invalid bytecode.
obelisk_rt_status
obelisk_rt_validate_bytecode_program(const obelisk_rt_bytecode_v1 &program,
                                     uint32_t continuation) noexcept;
bool obelisk_rt_validate_computed_wait_record(
    const obelisk_rt_execution_descriptor_v1 *execution,
    const obelisk_rt_computed_wait_record_v1 *wait, uint64_t available);

// Design-wide bytecode helpers shared by process construction/dispatch.
// Standalone descriptors receive full validation; context-bound dispatch
// reuses the image validated when that context was created.
obelisk_rt_status obelisk_rt_validate_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, uint64_t *outScratchSize,
    uint64_t *outScratchAlignment) noexcept;
obelisk_rt_status obelisk_rt_execute_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction) noexcept;
obelisk_rt_status obelisk_rt_execute_design_observer(
    const obelisk_rt_execution_descriptor_v1 &execution,
    obelisk_rt_context *context, uint32_t function,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCount) noexcept;
obelisk_rt_status
obelisk_rt_initialize_design_state(obelisk_rt_context *context) noexcept;

// Generated process spawns are already bound to a validated context. Reuse
// its immutable design-bytecode image while retaining the public standalone
// creation entry point for descriptors without a context.
extern "C" obelisk_rt_status
obelisk_rt_v1_process_instance_create_for_context(
    obelisk_rt_context *context,
    const obelisk_rt_process_descriptor_v1 *descriptor,
    obelisk_rt_process_instance_v1 **outInstance);
obelisk_rt_status obelisk_rt_resolve_design_drivers(obelisk_rt_context *context,
                                                    uint64_t begin,
                                                    uint64_t end) noexcept;
obelisk_rt_status
obelisk_rt_design_net_is_connected(obelisk_rt_context *context, uint64_t begin,
                                   uint64_t end, bool *outConnected) noexcept;
obelisk_rt_status obelisk_rt_run_one_design_task(
    obelisk_rt_context *context, uint32_t maximumRegion, uint32_t maximumRank,
    uint64_t maximumInsertionSequence, bool *outProgress) noexcept;
obelisk_rt_status
obelisk_rt_apply_managed_nba(obelisk_rt_context *context,
                             const ScheduledManagedNBA &update);

// Append one already-committed scalar transition while the context mutex is
// held, and latch level/iff observers against the state at this exact
// occurrence. Both native stores and design bytecode use this path.
bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown);
bool obelisk_rt_append_signal_event_unlocked(obelisk_rt_context *context,
                                             uint64_t bitOffset, bool oldValue,
                                             bool oldUnknown, bool newValue,
                                             bool newUnknown,
                                             bool evaluateComputedObservers);
bool obelisk_rt_publish_signal_occurrence_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edges, uint64_t *outSequence = nullptr);
// Publish one packed transition mask for a committed signal range. The three
// planes retain per-bit edge identity so a batched NBA cannot spuriously wake
// a posedge waiter because another bit in the range had a posedge.
bool obelisk_rt_publish_signal_transition_batch_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset = 0, uint64_t *outSequence = nullptr);
bool obelisk_rt_publish_native_signal_transition_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown,
    bool indexedExternalDeposit = false);
bool obelisk_rt_latch_conditional_signal_waiters_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint32_t edges);
bool obelisk_rt_latch_conditional_signal_range_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    uint32_t edges);
bool obelisk_rt_register_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch, uint64_t waiterToken = 0,
    bool designWaiter = false);
bool obelisk_rt_register_computed_signal_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_computed_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch);
void obelisk_rt_unregister_signal_wait_unlocked(
    obelisk_rt_context *context,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    uint64_t waiterToken = 0, bool designWaiter = false);
bool obelisk_rt_notify_observer_event_unlocked(obelisk_rt_context *context,
                                               uint64_t stableID);
bool obelisk_rt_notify_observer_signal_unlocked(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint64_t width);
bool obelisk_rt_evaluate_design_observers_unlocked(obelisk_rt_context *context,
                                                   uint32_t dependencyKind,
                                                   uint64_t publishedHandle,
                                                   uint64_t publishedWidth);
void obelisk_rt_erase_automatic_bookkeeping_unlocked(
    obelisk_rt_context *context, uint32_t automaticID);
obelisk_rt_status obelisk_rt_native_state_alloc_with_root_offsets(
    obelisk_rt_context *context, uint64_t bitWidth, const uint8_t *value,
    const uint8_t *unknown, std::vector<uint64_t> bitOffsets,
    uint64_t *outHandle);
obelisk_rt_status
obelisk_rt_native_state_alloc_managed(obelisk_rt_context *context,
                                      obelisk_rt_object_v1 *value,
                                      uint64_t *outHandle);
// Normalize a flat design-plane range to the stable identity used by native
// waits and publications. The caller holds context->mutex.
uint64_t
obelisk_rt_canonical_state_handle_unlocked(const obelisk_rt_context *context,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth) noexcept;
void obelisk_rt_invalidate_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth);

obelisk_rt_status obelisk_rt_force_design_nets(obelisk_rt_context *context,
                                               uint64_t begin, uint64_t width,
                                               const uint8_t *value,
                                               const uint8_t *unknown) noexcept;
obelisk_rt_status obelisk_rt_release_design_nets(obelisk_rt_context *context,
                                                 uint64_t begin,
                                                 uint64_t width) noexcept;

bool obelisk_rt_checked_design_record(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t offset,
    const uint8_t *&record, uint32_t &kind) noexcept;

obelisk_rt_status obelisk_rt_initialize_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    DesignDatabaseCache &cache) noexcept;
obelisk_rt_status obelisk_rt_register_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    const DesignDatabaseCache &cache) noexcept;
void obelisk_rt_unregister_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution) noexcept;
obelisk_rt_status
obelisk_rt_cached_design_root(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status
obelisk_rt_cached_design_child(const obelisk_rt_context *context,
                               obelisk_rt_design_cursor_v1 cursor,
                               obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status obelisk_rt_cached_design_child_at(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status obelisk_rt_cached_design_sibling(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status obelisk_rt_cached_design_lookup(
    const obelisk_rt_context *context, const uint8_t *name, uint64_t nameSize,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status
obelisk_rt_cached_design_info(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 cursor,
                              obelisk_rt_design_info_v1 *outInfo) noexcept;
obelisk_rt_status obelisk_rt_cached_design_type_info(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) noexcept;
obelisk_rt_status obelisk_rt_cached_design_type_child(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status obelisk_rt_cached_design_name(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outData, uint64_t *outSize) noexcept;

#endif // OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
