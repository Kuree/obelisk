//===- RuntimeInternal.h - Shared native runtime internals -------*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
#define OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H

#include "DesignBytecodeImage.h"
#include "ExceptionSupport.h"
#include "StrengthFormat.h"
#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Runtime/EvalNBAQueue.h"
#include "obelisk/Runtime/ReadySet.h"
#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if (defined(__clang__) || defined(__GNUC__)) && !defined(__wasm__)
#define OBELISK_RT_FEATURE_TEXT                                                \
  __attribute__((noinline, cold, section(".obelisk.feature.text")))
#define OBELISK_RT_FEATURE_HELPER                                              \
  __attribute__((section(".obelisk.feature.text")))
#define OBELISK_RT_RECURSIVE_BITSTREAM_TEXT                                    \
  __attribute__((noinline, cold,                                               \
                 section(".obelisk.feature.recursive_bitstream.text")))
#define OBELISK_RT_RECURSIVE_BITSTREAM_HELPER                                  \
  __attribute__((section(".obelisk.feature.recursive_bitstream.text")))
#elif defined(__clang__) || defined(__GNUC__)
// WebAssembly has a single code section, so it cannot provide the ELF-style
// feature text section above.  Keep feature services out of their callers and
// give the backend its portable cold-placement hint instead.
#define OBELISK_RT_FEATURE_TEXT __attribute__((noinline, cold))
#define OBELISK_RT_FEATURE_HELPER
#define OBELISK_RT_RECURSIVE_BITSTREAM_TEXT __attribute__((noinline, cold))
#define OBELISK_RT_RECURSIVE_BITSTREAM_HELPER
#else
#define OBELISK_RT_FEATURE_TEXT
#define OBELISK_RT_FEATURE_HELPER
#define OBELISK_RT_RECURSIVE_BITSTREAM_TEXT
#define OBELISK_RT_RECURSIVE_BITSTREAM_HELPER
#endif

constexpr uint64_t OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG =
    OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG;

/// Decode the fixed-width managed-word ABI without truncating it on 32-bit
/// targets. Heap objects are aligned and therefore always use tag zero.
inline obelisk_rt_object_v1 *
obelisk_rt_object_from_managed_word(obelisk_rt_managed_word_v1 word) noexcept {
  if (word > std::numeric_limits<uintptr_t>::max() || (word & UINT64_C(3)) != 0)
    return nullptr;
  return reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word));
}

inline obelisk_rt_managed_word_v1 obelisk_rt_managed_word_from_object(
    const obelisk_rt_object_v1 *object) noexcept {
  return static_cast<obelisk_rt_managed_word_v1>(
      reinterpret_cast<uintptr_t>(object));
}

// Preserve the native runtime's status-based allocation failure handling. The
// wasm runtime is compiled with -fno-exceptions and a noexcept libc++, so
// explicitly detected unrepresentable allocations follow the same fatal path
// as allocator failures there.
[[noreturn]] inline void obelisk_rt_out_of_memory() {
#if !defined(OBELISK_RT_IGNORE_EXCEPTIONS) &&                                  \
    (defined(__cpp_exceptions) || defined(_CPPUNWIND))
  throw std::bad_alloc();
#else
  std::fputs("obelisk runtime: out of memory\n", stderr);
  std::abort();
#endif
}

inline std::optional<uint64_t>
obelisk_rt_scan_raw_size(uint64_t bitWidth, bool fourState) noexcept {
  if (bitWidth == 0 || bitWidth > UINT64_MAX - 31)
    return std::nullopt;
  uint64_t words = (bitWidth + 31) / 32;
  uint64_t bytesPerWord = fourState ? 8 : 4;
  if (words > UINT64_MAX / bytesPerWord)
    return std::nullopt;
  return words * bytesPerWord;
}

inline bool obelisk_rt_decode_scan_raw(const char *input, uint64_t inputSize,
                                       uint64_t bitWidth, bool fourState,
                                       void *value, uint64_t valueSize,
                                       void *unknown,
                                       uint64_t unknownSize) noexcept {
  auto rawSize = obelisk_rt_scan_raw_size(bitWidth, fourState);
  if (!rawSize || inputSize != *rawSize || bitWidth > UINT64_MAX - 7)
    return false;
  uint64_t packedSize = (bitWidth + 7) / 8;
  if (packedSize > std::numeric_limits<size_t>::max() || !value || !unknown ||
      valueSize < packedSize || unknownSize < packedSize)
    return false;
  std::memset(value, 0, static_cast<size_t>(packedSize));
  std::memset(unknown, 0, static_cast<size_t>(packedSize));
  auto *valueBytes = static_cast<unsigned char *>(value);
  auto *unknownBytes = static_cast<unsigned char *>(unknown);
  uint64_t words = (bitWidth + 31) / 32;
  for (uint64_t word = 0; word != words; ++word) {
    uint32_t aval = 0;
    uint32_t bval = 0;
    uint64_t source = word * (fourState ? 8 : 4);
    std::memcpy(&aval, input + source, sizeof(aval));
    if (fourState)
      std::memcpy(&bval, input + source + sizeof(aval), sizeof(bval));
    uint32_t internal = fourState ? aval ^ bval : aval;
    uint64_t destination = word * sizeof(uint32_t);
    uint64_t count =
        std::min<uint64_t>(sizeof(uint32_t), packedSize - destination);
    std::memcpy(valueBytes + destination, &internal,
                static_cast<size_t>(count));
    std::memcpy(unknownBytes + destination, &bval, static_cast<size_t>(count));
  }
  if ((bitWidth & 7) != 0) {
    unsigned char mask =
        static_cast<unsigned char>((UINT32_C(1) << (bitWidth & 7)) - 1);
    valueBytes[packedSize - 1] &= mask;
    unknownBytes[packedSize - 1] &= mask;
  }
  return true;
}

struct SignalWaitLatch {
  bool triggered = false;
  bool affected = false;
};

struct SignalSubscription;

struct FileEntry {
  FILE *stream = nullptr;
  int lastError = 0;
  bool writable = false;
  bool readable = false;
  // One pushed-back byte for a descriptor opened without read access. The host
  // stream cannot hold it: glibc accepts ungetc() on a write-only stream and
  // then corrupts it on the next write.
  int pushback = -1;
  // Original SystemVerilog/VPI pathname. This is populated only when a file
  // is opened, so dormant VPI support adds no scheduler-side work.
  std::string name;
};

bool obelisk_rt_file_name_unlocked(obelisk_rt_context *context,
                                   uint32_t descriptor,
                                   std::string_view &name) noexcept;

// Parsed dynamic $sscanf/$fscanf formats are feature-local and immutable.
// Prefix strings are owned by the plan, so scanners may use their bytes after
// releasing the cache mutex. The cache itself is allocated only on the first
// dynamic-format scan and remains bounded independently of user input.
struct DynamicScanConversion {
  std::string prefix;
  uint64_t width = 0;
  uint32_t specifier = 0;
  bool suppressed = false;
};

struct DynamicScanPlan {
  std::string format;
  std::vector<DynamicScanConversion> conversions;
  std::string suffix;
  std::string error;
};

struct DynamicScanCacheEntry {
  uint64_t hash = 0;
  uint64_t size = 0;
  uint64_t identity = 0;
  obelisk_rt_string_v1 inlineValue = 0;
  std::shared_ptr<const DynamicScanPlan> plan;
};

struct DynamicScanState {
  void (*destroy)(DynamicScanState *) = nullptr;
  std::mutex mutex;
  std::vector<DynamicScanCacheEntry> plans;
  uint64_t parseCount = 0;
  uint64_t contentCompareBytes = 0;
};

obelisk_rt_status obelisk_rt_dynamic_scan_plan(
    obelisk_rt_context *context, obelisk_rt_string_v1 format,
    std::shared_ptr<const DynamicScanPlan> &plan) noexcept;

struct OwnedElementTypeDescriptor {
  obelisk_rt_element_type_v1 descriptor{};
  obelisk_rt_trace_layout_v1 trace{};
  std::vector<obelisk_rt_trace_entry_v1> entries;
  std::shared_ptr<const std::vector<uint64_t>> pattern;
};

// Append an IEEE-style recursive assignment-pattern representation of a
// managed sequential container. This is shared by display formatting while
// keeping the container storage layout private to Containers.cpp.
obelisk_rt_status obelisk_rt_container_pattern(obelisk_rt_object_v1 *container,
                                               std::string &output,
                                               unsigned depth);

struct CoverageTupleQueueSnapshot {
  uint64_t count = 0;
  uint64_t valueSize = 0;
  bool fourState = false;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

struct CoverageSetSnapshot {
  uint64_t count = 0;
  uint64_t valueSize = 0;
  uint64_t bitWidth = 0;
  uint32_t elementKind = 0;
  bool fourState = false;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

// Validate and copy one transient CrossQueueType before functional coverage
// resolution. Container internals remain private; Coverage.cpp receives only
// canonical logical-order planes and never retains the managed owner.
obelisk_rt_status obelisk_rt_coverage_tuple_queue_snapshot(
    obelisk_rt_context *context, obelisk_rt_object_v1 *queue,
    uint64_t expectedElementType, uint64_t expectedProvenanceSpan,
    bool expectedFourState, uint64_t maximumElements,
    CoverageTupleQueueSnapshot &output);

// Validate and copy one transient state-bin set expression before functional
// coverage resolution. Both dynamic arrays and queues are accepted and queue
// storage is linearized in logical index order. Coverage.cpp receives only
// canonical planes and never retains the managed owner.
obelisk_rt_status obelisk_rt_coverage_set_snapshot(
    obelisk_rt_context *context, obelisk_rt_object_v1 *container,
    uint64_t expectedBitWidth, uint32_t expectedElementKind,
    bool expectedFourState, uint64_t maximumElements,
    CoverageSetSnapshot &output);

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

  InsertResult insert(uint64_t token,
                      const obelisk_rt_random_state_v1 &randomState) {
    auto state = std::lower_bound(
        randomStates.begin(), randomStates.end(), token,
        [](const auto &entry, uint64_t value) { return entry.first < value; });
    if (state != randomStates.end() && state->first == token) {
      state->second = randomState;
      return insert(token);
    }
    randomStates.insert(state, {token, randomState});
    OBELISK_RT_TRY { return insert(token); }
    OBELISK_RT_CATCH_ALL {
      state = std::lower_bound(randomStates.begin(), randomStates.end(), token,
                               [](const auto &entry, uint64_t value) {
                                 return entry.first < value;
                               });
      if (state != randomStates.end() && state->first == token)
        randomStates.erase(state);
      OBELISK_RT_RETHROW;
    }
  }

  size_t erase(uint64_t token) {
    auto state = std::lower_bound(
        randomStates.begin(), randomStates.end(), token,
        [](const auto &entry, uint64_t value) { return entry.first < value; });
    if (state != randomStates.end() && state->first == token)
      randomStates.erase(state);
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
  void reserveRandomStates(size_t stateCount) {
    randomStates.reserve(stateCount);
  }
  size_t rangeCount() const { return ranges.size(); }
  size_t randomStateCount() const { return randomStates.size(); }
  obelisk_rt_random_state_v1 *randomState(uint64_t token) {
    auto found = std::lower_bound(
        randomStates.begin(), randomStates.end(), token,
        [](const auto &entry, uint64_t value) { return entry.first < value; });
    return found != randomStates.end() && found->first == token ? &found->second
                                                                : nullptr;
  }
  bool empty() const { return ranges.empty(); }

private:
  std::vector<std::pair<uint64_t, uint64_t>> ranges;
  std::vector<std::pair<uint64_t, obelisk_rt_random_state_v1>> randomStates;
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

    OBELISK_RT_TRY {
      Bucket &bucket = buckets[bucketIndex(capacity)];
      std::lock_guard<std::mutex> lock(bucket.mutex);
      bucket.buffers.push_back(std::move(buffer));
    }
    OBELISK_RT_CATCH_ALL {
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
      OBELISK_RT_SCHEDULE_INITIAL | OBELISK_RT_SCHEDULE_STARTUP |
      OBELISK_RT_SCHEDULE_DETACHED_CONTROLS |
      OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL | OBELISK_RT_SCHEDULE_ROOT;
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

// A live descriptor-loop ready cache may accept ordinary direct signal wakes
// without rescanning its existing cohort. The loop owns this transient record;
// unrelated selection-generation changes and reentry invalidate it.
struct NativeReadyPublicationBatch {
  uint64_t generation = 0;
  bool valid = false;
  std::vector<uint64_t> tokens;
  std::unordered_set<uint64_t> queued;
};

struct ScheduledProcess {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  std::vector<obelisk_rt_process_instance_v1 *> callers;
  // Control-stack size immediately before each corresponding task call.
  // This identifies the caller continuation to restore when a live task
  // activation is disabled from another logical process.
  std::vector<size_t> callerControlDepths;
  std::vector<uint64_t> controls;
  uint64_t token = 0;
  uint64_t parent = 0;
  uint64_t programOwner = 0;
  uint64_t observedEpoch = 0;
  uint64_t wakeTime = 0;
  uint64_t waitOffset = 0;
  uint64_t waitSize = 0;
  std::vector<uint64_t> waitGenerations;
  uint32_t waitOrderIndex = 0;
  std::vector<std::unique_ptr<SignalSubscription>> signalSubscriptions;
  std::unique_ptr<SignalWaitLatch> signalLatch;
  std::vector<std::pair<uint32_t, uint32_t>> continuationRanks;
  // AOT-owned actors may execute selected continuation activations through
  // bytecode without leaving the static schedule. The sorted table includes
  // continuation zero when the initial activation is bytecode-only.
  std::vector<uint32_t> bytecodeContinuations;
  uint32_t aotActorSlot = UINT32_MAX;
  // A task borrows the logical process, not the caller's generated frame
  // layout or continuation inventory. Park that binding until the outermost
  // task returns; unrelated AOT slots remain executable in the shared loop.
  uint32_t suspendedAOTActorSlot = UINT32_MAX;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  uint32_t scheduleRank = UINT32_MAX;
  uint64_t insertionSequence = 0;
  uint64_t waitSequence = 0;
  obelisk_rt_random_state_v1 random{};
  // Executable event-region ordinal. Normally the immutable home region,
  // temporarily an inactive region after #0 or an explicit resume override.
  uint32_t queuedRegion = 0;
  bool started = false;
  bool urgent = false;
  bool prioritySignal = false;
  bool signalTriggered = false;
  bool waitOrderReady = false;
  bool waitOrderFailed = false;
  bool initialProcess = false;
  bool startupProcess = false;
  bool rootProcess = false;
  bool explicitlySuspended = false;
  bool computedObserverWaitRegistered = false;
};

obelisk_rt_status obelisk_rt_mailbox_wait_ready(obelisk_rt_object_v1 *mailbox,
                                                uint32_t predicate,
                                                bool &ready);
obelisk_rt_status
obelisk_rt_semaphore_keys_ready(obelisk_rt_object_v1 *semaphore, int32_t keys,
                                bool &ready);
obelisk_rt_status
obelisk_rt_semaphore_try_get_raw(obelisk_rt_object_v1 *semaphore, int32_t keys,
                                 uint32_t *outSuccess);
obelisk_rt_status
obelisk_rt_semaphore_wait_ready(obelisk_rt_context *context,
                                obelisk_rt_object_v1 *semaphore, int32_t keys,
                                uint64_t waitSequence, bool &ready);
obelisk_rt_status
obelisk_rt_semaphore_wait_acquire(const obelisk_rt_wait_record_v1 *wait,
                                  bool &acquired);

struct SignalValueSnapshot {
  uint64_t sequence = 0;
  bool value = false;
  bool unknown = false;
};

struct NativeAutomaticState {
  struct CandidateRoot {
    uint64_t byteOffset;
    uint32_t allowedKinds;
  };
  uint64_t bitWidth = 0;
  obelisk_rt_process_instance_v1 *owner = nullptr;
  uint64_t designOwner = 0;
  uint64_t referenceCount = 1;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  obelisk_rt_object_v1 *managedValue = nullptr;
  bool managedRootRegistered = false;
  std::vector<uint64_t> managedRootByteOffsets;
  std::vector<CandidateRoot> candidateRootByteOffsets;
};

struct EventState {
  uint64_t generation = 0;
  uint64_t lastTriggeredTime = 0;
};

struct InertialDriverSite {
  uint64_t codeUnit = 0;
  uint32_t component = 0;
  bool pathStorage = false;

  bool operator==(const InertialDriverSite &other) const {
    return codeUnit == other.codeUnit && component == other.component &&
           pathStorage == other.pathStorage;
  }
};

struct InertialDriverSiteHash {
  size_t operator()(const InertialDriverSite &site) const {
    uint64_t mixed = site.codeUnit ^
                     (uint64_t{site.component} + UINT64_C(0x9e3779b97f4a7c15) +
                      (site.codeUnit << 6) + (site.codeUnit >> 2));
    mixed ^= uint64_t{site.pathStorage} * UINT64_C(0xd6e8feb86659fd93);
    return static_cast<size_t>(mixed);
  }
};

struct InertialDriverPending {
  uint64_t destination = UINT64_MAX;
  uint64_t width = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  uint64_t remaining = 0;
  uint64_t secondDestination = UINT64_MAX;
  std::vector<uint8_t> secondValue;
  std::vector<uint8_t> secondUnknown;
};

struct InertialPathPending {
  uint64_t destination = UINT64_MAX;
  uint64_t width = 0;
  bool pulseControlled = false;
  uint32_t nextGroup = 0;
  uint32_t groupCount = 0;
  std::vector<uint64_t> generation;
  std::vector<uint8_t> targetValue;
  std::vector<uint8_t> targetUnknown;
  std::vector<uint8_t> valid;
  std::vector<uint8_t> delayed;
  std::vector<uint8_t> needsSchedule;
  std::vector<uint64_t> candidateDelay;
  std::vector<uint64_t> candidatePulseReject;
  std::vector<uint64_t> candidatePulseError;
  std::vector<uint8_t> candidatePulseFlags;
  std::vector<uint8_t> candidateFromSymbol;
  std::vector<uint64_t> scheduledDueTime;
  std::vector<uint64_t> scheduledSequence;
  std::vector<std::vector<uint64_t>> liveSequences;
};

struct InertialStrengthPathPending {
  uint64_t lowDestination = UINT64_MAX;
  uint64_t highDestination = UINT64_MAX;
  uint64_t width = 0;
  uint32_t nextGroup = 0;
  uint32_t groupCount = 0;
  bool pulseControlled = false;
  std::vector<uint8_t> lowValue;
  std::vector<uint8_t> lowUnknown;
  std::vector<uint8_t> highValue;
  std::vector<uint8_t> highUnknown;
  std::vector<uint8_t> transitionValue;
  std::vector<uint8_t> transitionUnknown;
  std::vector<uint8_t> valid;
  std::vector<uint8_t> needsSchedule;
  std::vector<uint64_t> candidateDelay;
  std::vector<uint64_t> candidatePulseReject;
  std::vector<uint64_t> candidatePulseError;
  std::vector<uint8_t> candidatePulseFlags;
  std::vector<uint8_t> candidateFromSymbol;
  std::vector<uint64_t> generation;
  std::vector<uint64_t> scheduledDueTime;
  std::vector<uint64_t> lowSequence;
  std::vector<uint64_t> highSequence;
  std::vector<std::vector<uint64_t>> liveSequences;
};

struct InertialNetPending {
  bool value = false;
  bool unknown = false;
  bool chargeDecay = false;
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
  uint64_t clockingOutput = UINT64_MAX;
  uint32_t execRegion = OBELISK_RT_REGION_NBA;
  uint32_t retainedAutomaticID = 0;
  uint8_t *valuePlane = nullptr;
  uint8_t *unknownPlane = nullptr;
  uint64_t planeBitCount = 0;
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  bool stringValue = false;
  bool driver = false;
  bool deferDriverResolution = false;
  bool forceDriverResolution = false;
  bool publishDriverTransition = false;
  uint32_t realWidth = 0;
  bool managedValue = false;
  bool inlinePacked = false;
  obelisk_rt_string_v1 rootedString = 0;
  obelisk_rt_object_v1 *rootedManaged = nullptr;
  uint64_t inlineValue = 0;
  uint64_t inlineUnknown = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  InertialDriverSite inertialSite{UINT64_MAX, 0, false};
  bool inertialDriverVector = false;
  bool inertialDriverInitialProjection = false;
  bool inertialPathDriver = false;
  bool inertialPathStrengthPair = false;
  bool inertialPathStrengthFinal = false;
  uint64_t inertialPathBit = UINT64_MAX;
  uint64_t inertialPathGeneration = 0;
  uint64_t inertialNetBit = UINT64_MAX;
  uint64_t inertialNetGroup = UINT64_MAX;
  uint64_t inertialNetCancelGroup = UINT64_MAX;
  bool inertialNetInitialProjection = false;
  bool inertialNetChargeDecay = false;
  bool cancelled = false;
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
  // Staged bits rewritten with a different value since the last barrier.
  // The merge keeps only the final value; the commit reports these bits as
  // changed so change waiters still see a round trip (IEEE 1800-2017 4.6(b)).
  std::vector<uint64_t> transient;
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

struct ScheduledPassSwitchEvent {
  uint32_t passSwitchID = UINT32_MAX;
  uint8_t state = 0;
  uint64_t delayedMosEdge = UINT64_MAX;
  uint16_t strengths = 0;
};

using ScheduledPassSwitchEvents =
    std::map<std::pair<uint64_t, uint64_t>, ScheduledPassSwitchEvent>;

struct DelayedPassSwitchPending {
  ScheduledPassSwitchEvents::iterator event;
  uint8_t state = 0;
};

struct DelayedMosPending {
  ScheduledPassSwitchEvents::iterator event;
  uint16_t strengths = 0;
};

struct DelayedMosControl {
  uint8_t state = 3;
  std::array<uint64_t, 3> delays{};
};

struct DesignActivation {
  uint32_t function = 0;
  uint32_t continuation = 0;
  std::vector<uint8_t> frame;
  uint64_t scratchOffset = 0;
  uint64_t scratchSize = 0;
  uint32_t scheduleRank = UINT32_MAX;
  size_t controlDepth = 0;
};

struct ScheduledDesignTask {
  uint64_t id = 0;
  uint64_t parent = 0;
  uint64_t programOwner = 0;
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
  uint32_t waitOrderIndex = 0;
  std::vector<std::unique_ptr<SignalSubscription>> signalSubscriptions;
  std::unique_ptr<SignalWaitLatch> signalLatch;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t phase = 0;
  uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
  uint32_t scheduleRank = UINT32_MAX;
  uint32_t queuedRegion = 0;
  uint64_t insertionSequence = 0;
  uint64_t waitSequence = 0;
  obelisk_rt_random_state_v1 random{};
  bool started = false;
  bool urgent = false;
  bool terminated = false;
  bool signalTriggered = false;
  bool waitOrderReady = false;
  bool waitOrderFailed = false;
  bool startupProcess = false;
  bool prioritySignal = false;
  bool explicitlySuspended = false;
  bool computedObserverWaitRegistered = false;
};

// Cold, bytecode-only scheduler state for a large direct-signal publication
// cohort. The generic scheduler owns no instance of this state until more
// than a small candidate set becomes ready at once.
struct DesignReadyCohortEntry {
  uint64_t id = 0;
  uint32_t region = UINT32_MAX;
  uint32_t rank = UINT32_MAX;
  uint64_t insertionSequence = UINT64_MAX;
};

struct DesignReadyCohortState {
  // Stored later-first so the exact next scheduler key is consumed from the
  // back in O(1). Cohorts contain only ordinary direct CHANGE/EDGE resumes;
  // every other readiness source stays on the exact general scan.
  std::vector<DesignReadyCohortEntry> ready;
  // Candidates outside the homogeneous direct-signal batch are rescanned
  // exactly on every cached selection and compared with the cached head.
  std::vector<uint64_t> slowCandidates;
  uint64_t selectionGeneration = 0;
  uint64_t schedulerTime = 0;
  uint64_t nextDesignTaskID = 0;
  size_t suppressedCandidateHighWater = 0;
  bool runningFinals = false;
  bool valid = false;
  bool suppressed = false;
  bool persistentSuppression = false;
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
    NativeManagedWait,
    DesignManagedWait,
  };

  uint64_t stableID = 0;
  uint64_t bitWidth = 0;
  uint64_t lastExaminedSequence = 0;
  uint64_t waiterToken = 0;
  uint32_t edge = 0;
  Target target = NativeDirectWait;
  bool suppressActiveSelf = false;
  bool outsideStaticFanout = false;
  SignalWaitLatch *latch = nullptr;
  std::vector<SignalSubscriptionBucketSlot> bucketSlots;
};

static_assert(sizeof(void *) != 8 || sizeof(SignalSubscription) == 72,
              "clock occurrence metadata must not grow ordinary waits");

struct ClockOccurrenceCondition {
  uint64_t stableID = UINT64_MAX;
  uint32_t width = 0;
  uint32_t predicate = OBELISK_RT_WAIT_CONDITION_KNOWN_ONE;
  uint64_t observerCodeUnitID = 0;
  std::vector<obelisk_rt_computed_capture_v1> observerCaptures;

  bool isObserver() const { return observerCodeUnitID != 0; }
};

struct ClockOccurrenceWaveKey {
  uint64_t time = 0;
  uint64_t progress = 0;
  uint32_t region = UINT32_MAX;

  bool operator==(const ClockOccurrenceWaveKey &other) const {
    return time == other.time && progress == other.progress &&
           region == other.region;
  }
};

struct ClockOccurrenceWaitState {
  uint64_t occurrenceSite = 0;
  ClockOccurrenceWaveKey currentKey;
  bool hasCurrentKey = false;
  std::array<uint64_t, 64> occurrenceCounts{};
  std::vector<uint64_t> currentCohorts;
  std::vector<uint64_t> finalizedCohorts;
  size_t consumedCohorts = 0;
  std::vector<ClockOccurrenceCondition> conditions;
  uint64_t conditionMask = 0;
};

struct ClockOccurrenceSubscription {
  uint64_t stableID = 0;
  uint64_t bitWidth = 0;
  uint64_t lastExaminedSequence = 0;
  uint64_t waiterToken = 0;
  uint32_t edge = 0;
  uint8_t occurrenceBit = 0;
  bool native = false;
  bool clockingOutputSeen = false;
  // Token zero denotes a design-lifetime clocking-output tracker rather than
  // a suspend.clock_set waiter. It records only matching numeric time and is
  // queried by zero-skew output helpers in the same time slot.
  uint64_t lastClockingOutputTime = 0;
  // Clause 31.5 state exists only for a noncanonical descriptor subscription.
  // Six standard transition classes fit in the frozen edge word; three packed
  // feature-local planes retain value, unknown, and initialization without
  // changing the ordinary wait ABI.
  std::vector<uint8_t> previousValue;
  std::vector<uint8_t> previousUnknown;
  std::vector<uint8_t> previousInitialized;
  std::vector<SignalSubscriptionBucketSlot> bucketSlots;
};

struct ClockingOutputOccurrenceKey {
  uint64_t stableID = UINT64_MAX;
  uint64_t bitWidth = 0;
  uint32_t edge = 0;

  bool operator==(const ClockingOutputOccurrenceKey &other) const {
    return stableID == other.stableID && bitWidth == other.bitWidth &&
           edge == other.edge;
  }
};

struct ClockingOutputOccurrenceKeyHash {
  size_t operator()(const ClockingOutputOccurrenceKey &key) const {
    size_t hash = std::hash<uint64_t>{}(key.stableID);
    hash ^= std::hash<uint64_t>{}(key.bitWidth) + size_t{0x9e3779b9} +
            (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t>{}(key.edge) + size_t{0x9e3779b9} + (hash << 6) +
            (hash >> 2);
    return hash;
  }
};

struct ClockOccurrenceBucketEntry {
  ClockOccurrenceSubscription *subscription = nullptr;
  size_t slotIndex = 0;
};

// One stack-owned publication view exposed only while a compiled Clause 31.7
// condition is evaluated. The lazy clock-occurrence feature retains the
// pointer; observer loads merge overlapping bits without copying publication
// planes. Nested event evaluation walks this stack-only chain. Ordinary state
// loads see a null pointer.
struct ClockConditionPublicationView {
  uint64_t stableID = UINT64_MAX;
  uint64_t bitWidth = 0;
  uint64_t planeBitOffset = 0;
  const uint8_t *newValue = nullptr;
  const uint8_t *newUnknown = nullptr;
  // Synchronous sampler evaluation may itself publish another signal before
  // the outer publication commits canonical storage. Retain the enclosing
  // views so nested evaluators observe every post-transition value at their
  // exact event instant; the newest overlapping publication wins.
  const ClockConditionPublicationView *previous = nullptr;
};

struct CovergroupClockEventRegistration;

struct CovergroupClockEventClause {
  CovergroupClockEventRegistration *registration = nullptr;
  uint32_t clauseIndex = 0;
  uint64_t lastExaminedSequence = 0;
  std::vector<SignalSubscriptionBucketSlot> bucketSlots;
};

struct CovergroupClockEventRegistration {
  uint64_t ownerLogicalToken = 0;
  uint64_t observerCodeUnitID = 0;
  uint64_t covergroupHandle = 0;
  uint64_t lastSampledSequence = 0;
  uint64_t lastStrobeTime = 0;
  bool native = false;
  bool strobe = false;
  bool strobePending = false;
  bool lastStrobeTimeValid = false;
  // The codec is byte-addressed, but observers and clauses are read as their
  // fixed-width v1 records. Own the image in aligned storage so those typed
  // views remain well-defined on hosts with strict alignment requirements.
  std::vector<uint64_t> eventPlan;
  std::vector<obelisk_rt_computed_capture_v1> captures;
  std::vector<std::unique_ptr<CovergroupClockEventClause>> clauses;
};

struct CovergroupClockEventBucketEntry {
  CovergroupClockEventClause *clause = nullptr;
  size_t slotIndex = 0;
};

// Cold, process-owned subscriptions for IEEE 1800 covergroup clock-event
// sampling. A separate index keeps ordinary signal waits and assertion clock
// cohorts unchanged; signal publication pays only one null pointer check when
// no clocked covergroup exists. Non-strobe registrations sample at publication
// time. Strobe registrations retain one pending bit until the scheduler enters
// the Postponed region.
struct CovergroupClockEventFeatureState {
  std::unordered_map<uint64_t,
                     std::unique_ptr<CovergroupClockEventRegistration>>
      registrations;
  std::unordered_map<SignalSubscriptionBucketKey,
                     std::vector<CovergroupClockEventBucketEntry>,
                     SignalSubscriptionBucketKeyHash>
      subscriptionBuckets;
  uint64_t pendingStrobeCount = 0;
};

using ReplaceableEventCalendar =
    std::map<std::pair<uint64_t, uint64_t>, ScheduledDesignEvent>;

struct ReplaceableEventPending {
  uint64_t ownerToken = 0;
  uint64_t generation = 0;
  bool scheduled = false;
  ReplaceableEventCalendar::iterator event;
};

struct ReplaceableEventFeatureState {
  ReplaceableEventCalendar calendar;
  // One fixed record per compiler-private static timer retains the monotonic
  // generation after cancel/maturity. This bounded inventory is released with
  // the owning clock-occurrence feature during process teardown.
  std::unordered_map<uint64_t, ReplaceableEventPending> pending;
  // Per-owner inventory makes coordinator teardown proportional only to that
  // coordinator's fixed static timer count. Calendar removal remains O(log N)
  // and another coordinator's pending deadline is never inspected or erased.
  std::unordered_map<uint64_t, std::vector<uint64_t>> ownedTimers;
};

struct NoChangeDataOccurrence {
  uint64_t time = 0;
  uint64_t count = 0;
};

struct NoChangeClosedWindow {
  __int128 begin = 0;
  __int128 end = 0;
  uint64_t multiplicity = 0;
};

struct NoChangeCheckState {
  int64_t startOffset = 0;
  int64_t endOffset = 0;
  bool initialized = false;
  bool open = false;
  __int128 openBegin = 0;
  size_t openDataBegin = 0;
  uint64_t pendingReports = 0;
  std::vector<NoChangeDataOccurrence> data;
  size_t dataBegin = 0;
  std::vector<NoChangeClosedWindow> windows;
};

struct NoChangeCheckKey {
  uint64_t logicalToken = 0;
  uint64_t occurrenceSite = 0;

  bool operator==(const NoChangeCheckKey &other) const {
    return logicalToken == other.logicalToken &&
           occurrenceSite == other.occurrenceSite;
  }
};

struct NoChangeCheckKeyHash {
  size_t operator()(const NoChangeCheckKey &key) const {
    size_t first = std::hash<uint64_t>{}(key.logicalToken);
    size_t second = std::hash<uint64_t>{}(key.occurrenceSite);
    return first ^ (second + size_t{0x9e3779b9} + (first << 6) + (first >> 2));
  }
};

struct NoChangeFeatureState {
  std::unordered_map<NoChangeCheckKey, NoChangeCheckState, NoChangeCheckKeyHash>
      checks;
};

struct ClockOccurrenceFeatureState {
  std::unordered_map<uint64_t, ClockOccurrenceWaitState> waits;
  std::unordered_map<SignalSubscriptionBucketKey,
                     std::vector<ClockOccurrenceBucketEntry>,
                     SignalSubscriptionBucketKeyHash>
      subscriptionBuckets;
  std::unordered_map<uint64_t,
                     std::vector<std::unique_ptr<ClockOccurrenceSubscription>>>
      subscriptions;
  std::unordered_map<ClockingOutputOccurrenceKey, ClockOccurrenceSubscription *,
                     ClockingOutputOccurrenceKeyHash>
      clockingOutputs;
  uint64_t conditionalWaitCount = 0;
  // Timer-mode Clause 31.4.2/.3 checks are the only users of replaceable
  // named-event deadlines. Keep their indexed calendar behind the already
  // cold clock-occurrence feature and one further null pointer so ordinary
  // waits, named events, and event-based timing checks construct no map.
  std::unique_ptr<ReplaceableEventFeatureState> replaceableEvents;
};

static_assert(
    sizeof(ClockOccurrenceFeatureState) ==
        ((sizeof(decltype(ClockOccurrenceFeatureState::waits)) +
          sizeof(decltype(ClockOccurrenceFeatureState::subscriptionBuckets)) +
          sizeof(decltype(ClockOccurrenceFeatureState::subscriptions)) +
          sizeof(decltype(ClockOccurrenceFeatureState::clockingOutputs)) +
          sizeof(uint64_t) +
          sizeof(decltype(ClockOccurrenceFeatureState::replaceableEvents)) +
          alignof(ClockOccurrenceFeatureState) - 1) /
         alignof(ClockOccurrenceFeatureState)) *
            alignof(ClockOccurrenceFeatureState),
    "$nochange must not add storage to the ordinary clock feature");

constexpr uint64_t kRecursiveWatchGroupBit = UINT64_C(1) << 63;
constexpr uint64_t kClassWatchGroupBit = UINT64_C(1) << 62;
using RecursiveWatchGroupVisit = bool (*)(void *, uint64_t);

// Feature-owned state for compact recursive managed-watch groups. The cold
// bit-stream service supplies the concrete storage and destructor; ordinary
// designs retain only this null tail pointer.
struct RecursiveWatchGroupState {
  void (*destroy)(RecursiveWatchGroupState *) noexcept = nullptr;
};

// Opaque cold state owned by the class bit-stream feature object. Keeping only
// a destroy callback in the common runtime avoids a dependency on its parser
// or execution engine when the feature anchor is absent.
struct ClassBitstreamState {
  void (*destroy)(ClassBitstreamState *) noexcept = nullptr;
  void (*notifyRange)(ClassBitstreamState *, obelisk_rt_context *, uint64_t,
                      uint64_t, uint64_t) noexcept = nullptr;
};

struct SignalSubscriptionDiagnostics {
  uint64_t publications = 0;
  uint64_t subscriptionsCurrent = 0;
  uint64_t subscriptionsHighWater = 0;
  uint64_t subscribersExamined = 0;
  uint64_t readinessCalls = 0;
  uint64_t candidateScans = 0;
  uint64_t candidateInventoryVisits = 0;
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
  uint64_t aotCheckpoints = 0;
  uint64_t aotTerminalCheckpoints = 0;
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
      obelisk_rt_out_of_memory();
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
  uint64_t owner = 0;
  uint32_t continuation = 0;
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

// Dynamic DPI call state is shared with the cold export service. Keeping the
// export implementation in a separate archive member lets import-only and
// no-DPI executables avoid linking it.
struct ActiveDpiCall {
  obelisk_rt_context *context = nullptr;
  DpiScopeHandle *scope = nullptr;
  std::string callerFile;
  uint32_t callerLine = 0;
  obelisk_rt_status exportStatus = OBELISK_RT_OK;
  uint32_t importFlags = 0;
  bool disabledState = false;
  bool disableAcknowledged = false;
  ActiveDpiCall *previous = nullptr;
};

extern thread_local ActiveDpiCall *activeDpiCall;

// The bytecode interpreter is always linked, while DPI aggregate marshalling
// is pay-for-play. A DPI bytecode feature anchor installs these pointers;
// ordinary designs retain null pointers and no undefined DPI symbols.
extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_pack)
    designBytecodeDpiOpenAggregatePack;
extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_unpack)
    designBytecodeDpiOpenAggregateUnpack;
extern decltype(&obelisk_rt_v1_dpi_aggregate_pack)
    designBytecodeDpiAggregatePack;
extern decltype(&obelisk_rt_v1_dpi_aggregate_unpack)
    designBytecodeDpiAggregateUnpack;
extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_roots_push)
    designBytecodeDpiOpenAggregateRootsPush;
extern decltype(&obelisk_rt_v1_dpi_aggregate_roots_pop)
    designBytecodeDpiAggregateRootsPop;
extern decltype(&obelisk_rt_v1_dpi_aggregate_state_alloc)
    designBytecodeDpiAggregateStateAlloc;

class ManagedHeap;

struct NetAliasRange {
  uint64_t valueOffset = 0;
  uint64_t targetOffset = 0;
  uint64_t width = 0;
  bool fourState = false;
  bool bitwiseDelay = false;
  std::vector<std::optional<std::array<uint64_t, 3>>> propagationDelays;
};

struct NetDriverBit {
  uint64_t valueOffset = 0;
  uint8_t strength0 = 6;
  uint8_t strength1 = 6;
};

struct NetStrengthDriverPairRange {
  uint64_t lowOffset = 0;
  uint64_t highOffset = 0;
  uint64_t width = 0;
  uint8_t lowStrength0 = 6;
  uint8_t lowStrength1 = 6;
  uint8_t highStrength0 = 6;
  uint8_t highStrength1 = 6;
};

struct NetPassNeighbor {
  uint64_t root = 0;
  uint32_t passSwitchId = 0;
  bool resistive = false;
  bool controlled = false;
  // A directed MOS edge is retained in both adjacency lists for component
  // discovery, but contributes only where `receives` is true.
  bool directed = false;
  bool receives = true;
};

struct NetControlledPassEdge {
  uint64_t component = 0;
  uint32_t lhs = 0;
  uint32_t rhs = 0;
  bool resistive = false;
  bool directed = false;
};

struct NetDelayedMosEdge {
  uint64_t key = UINT64_MAX;
  uint32_t control = 0;
  uint64_t sourceRoot = 0;
  uint64_t destinationRoot = 0;
  bool resistive = false;
};

struct NetPassComponent {
  struct ReachableSource {
    uint32_t index = 0;
    uint8_t definiteReduction = 5;
    uint8_t possibleReduction = 5;
  };
  std::vector<uint64_t> roots;
  // Direct-edge reference counts make one controlled switch update O(1) per
  // scalar edge even in the presence of parallel devices. Resolution reads
  // only the two precomputed closures below.
  std::vector<uint32_t> definiteNonresistive;
  std::vector<uint32_t> definiteResistive;
  std::vector<uint32_t> possibleNonresistive;
  std::vector<uint32_t> possibleResistive;
  std::vector<uint8_t> reductions;
  std::vector<uint8_t> possibleReductions;
  // Sparse rows keep resolution proportional to electrically reachable
  // sources. This matters for a common MOS fanout: N one-way outputs share a
  // source, but each output has only two contributing roots rather than N.
  std::vector<std::vector<ReachableSource>> reachableSources;
};

struct NetAliasCache {
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  std::unordered_map<uint64_t, uint64_t> rootByBit;
  std::unordered_map<uint64_t, std::vector<uint64_t>> members;
  std::unordered_map<uint64_t, std::vector<NetDriverBit>> driverBits;
  std::unordered_map<uint64_t, std::vector<NetPassNeighbor>> passNeighbors;
  std::unordered_map<uint64_t, uint64_t> passComponentByRoot;
  std::unordered_map<uint64_t, NetPassComponent> passComponents;
  std::unordered_map<uint32_t, std::vector<NetControlledPassEdge>>
      controlledPassEdges;
  std::unordered_map<uint32_t, uint8_t> controlledPassStates;
  std::unordered_map<uint64_t, NetDelayedMosEdge> delayedMosEdges;
  std::unordered_map<uint32_t, std::vector<uint64_t>> delayedMosByControl;
  std::unordered_map<uint64_t, std::vector<uint64_t>> delayedMosBySource;
  std::unordered_map<uint64_t, std::vector<uint64_t>> delayedMosByDestination;
  std::unordered_map<uint64_t, std::vector<uint64_t>> uniformDelayedRootsByRoot;
  std::unordered_map<uint64_t, uint8_t> resolutionByRoot;
  std::unordered_map<uint64_t, uint8_t> chargeStrengthByBit;
  std::vector<NetAliasRange> nets;
  std::vector<NetAliasRange> drivers;
  std::vector<NetStrengthDriverPairRange> strengthDriverPairs;
};

// Decoded view of the immutable reflection image. Context creation validates
// the complete image before publishing this cache; context-owned consumers can
// therefore perform constant-time structural checks without re-checksumming or
// re-walking the design on every query.
// Waveform dump state. Defined in VCD.cpp; the context only owns the pointer.
struct VCDTraceState;
struct EVCDTraceState;

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
  uint64_t statements = 0;
  uint64_t statementCount = 0;
  uint64_t statementSites = 0;
  uint64_t statementSiteCount = 0;
  uint64_t relations = 0;
  uint64_t relationCount = 0;
  uint64_t semanticTypes = 0;
  uint64_t semanticTypeCount = 0;
  uint64_t semanticTypeEdges = 0;
  uint64_t semanticTypeEdgeCount = 0;
  uint64_t semanticRootBindings = 0;
  uint64_t semanticRootBindingCount = 0;
  uint64_t relationIndices = 0;
  uint64_t relationIndexCount = 0;
  uint64_t relationIndexDimensions = 0;
  uint64_t relationIndexDimensionCount = 0;
  uint64_t relationIndexKeys = 0;
  uint64_t relationIndexKeyCount = 0;
  uint64_t relationIndexMembers = 0;
  uint64_t relationIndexMemberCount = 0;
  uint64_t fixedProperties = 0;
  uint64_t fixedPropertyCount = 0;
  uint64_t resolvedNetRuns = 0;
  uint64_t resolvedNetRunCount = 0;
  uint64_t netDelayRuns = 0;
  uint64_t netDelayRunCount = 0;
  uint64_t staticObjects = 0;
  uint64_t staticObjectCount = 0;
  uint64_t definitions = 0;
  uint64_t definitionCount = 0;
  uint64_t definitionBindings = 0;
  uint64_t definitionBindingCount = 0;
  uint64_t definitionMembers = 0;
  uint64_t definitionMemberCount = 0;
  uint64_t definitionMemberRelations = 0;
  uint64_t definitionMemberRelationCount = 0;
  uint64_t definitionMemberRelationTargets = 0;
  uint64_t definitionMemberRelationTargetCount = 0;
  uint64_t definitionSpecializations = 0;
  uint64_t definitionSpecializationCount = 0;
  uint64_t definitionSpecializationBindings = 0;
  uint64_t definitionSpecializationBindingCount = 0;
  uint64_t definitionMemberEndpoints = 0;
  uint64_t definitionMemberEndpointCount = 0;
  uint64_t definitionMemberInstanceRelations = 0;
  uint64_t definitionMemberInstanceRelationCount = 0;
  uint64_t definitionMemberInstanceRelationTargets = 0;
  uint64_t definitionMemberInstanceRelationTargetCount = 0;
  uint64_t definitionMemberInstanceRelationInverses = 0;
  uint64_t definitionMemberInstanceRelationInverseCount = 0;
  uint64_t frozenValues = 0;
  uint64_t frozenValueCount = 0;
  uint64_t frozenValueBindings = 0;
  uint64_t frozenValueBindingCount = 0;
  uint64_t frozenValuePayload = 0;
  uint64_t frozenValuePayloadSize = 0;
  uint64_t stateBitCount = 0;
  bool validated = false;
};

// A validated half-open range in the immutable design relation table. These
// views are created only by explicit VPI queries; keeping them as table
// indices avoids materializing per-iterator cursor vectors.
struct VPIRelationRange {
  uint64_t first = 0;
  uint64_t count = 0;
};

struct FunctionalCoverageValue {
  uint64_t id = 0;
  uint64_t bitWidth = 0;
  uint64_t valueSize = 0;
  uint32_t kind = 0;
  uint32_t argumentRefKind = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  obelisk_rt_object_v1 *owner = nullptr;
  uint64_t payload = 0;
};

struct VPIFixedPropertyValue {
  uint8_t kind = 0;
  uint64_t payload = 0;
  const uint8_t *stringData = nullptr;
  uint64_t stringSize = 0;
};

struct VPIFrozenValue {
  uint32_t kindAndFlags = 0;
  uint64_t bitWidth = 0;
  const uint8_t *payload = nullptr;
  uint64_t payloadSize = 0;
};

struct VPINetDelayValue {
  int64_t rise = 0;
  int64_t fall = 0;
  int64_t third = 0;
};

struct VPIRelationIndexInfo {
  uint32_t firstDimension = UINT32_MAX;
  uint32_t firstKey = UINT32_MAX;
  uint32_t firstOrdinalKey = UINT32_MAX;
  uint32_t elementCount = 0;
  uint16_t dimensionCount = 0;
  bool sparse = false;
};

struct VPIArrayMemberInfo {
  obelisk_rt_design_cursor_v1 array{};
  VPIRelationIndexInfo index{};
  uint32_t arrayType = 0;
  uint32_t ordinal = 0;
};

struct FunctionalCoverageBinState {
  uint64_t id = 0;
  uint64_t templateBin = 0;
  uint64_t item = 0;
  uint64_t valueSet = 0;
  uint64_t atLeast = 1;
  obelisk::coverage::FunctionalBinKind kind =
      obelisk::coverage::FunctionalBinKind::State;
  uint32_t flags = 0;
  uint64_t count = 0;
  bool overflow = false;
  bool excluded = false;
};

struct FunctionalCoverageCrossState {
  struct TargetConstraint {
    uint32_t targetOrdinal = 0;
    std::vector<uint64_t> selectedTargetBins;
  };

  struct Rectangle {
    std::vector<TargetConstraint> constraints;
  };

  struct ExplicitBin {
    uint64_t bin = 0;
    std::vector<Rectangle> alternatives;
  };

  uint64_t id = 0;
  uint64_t templateItem = 0;
  uint64_t atLeast = 1;
  std::vector<uint64_t> targets;
  std::vector<uint64_t> automaticBinCount;
  uint64_t automaticRoot = 0;
  std::vector<ExplicitBin> explicitBins;
  bool excluded = false;
};

struct FunctionalCoverageCrossTupleState {
  uint64_t count = 0;
  bool overflow = false;
};

struct FunctionalCoverageTypeState {
  obelisk::coverage::Digest configuration{};
  uint32_t instanceGoal = 100;
  uint32_t instanceWeight = 1;
  uint32_t typeGoal = 100;
  uint32_t typeWeight = 1;
  bool mergeInstances = false;
  bool getInstCoverage = false;
  bool strobe = false;
  uint64_t groupCrossNumPrintMissing = 0;
  std::unordered_map<uint64_t, uint64_t> crossNumPrintMissing;
  std::unordered_set<uint64_t> explicitCrossNumPrintMissingItems;
  std::vector<uint64_t> items;
  std::vector<uint8_t> itemAggregating;
  std::vector<uint32_t> itemGoals;
  std::vector<uint32_t> itemWeights;
  std::vector<uint64_t> itemAtLeast;
  std::vector<uint32_t> typeItemGoals;
  std::vector<uint32_t> typeItemWeights;
  /// Original instance-profile index when this is a one-item projection used
  /// to implement a coverpoint or cross method query.
  size_t projectedItemIndex = SIZE_MAX;
  std::vector<FunctionalCoverageBinState> bins;
  std::vector<FunctionalCoverageCrossState> crosses;
  std::vector<uint64_t> instances;
  /// Resolved ordinary transition alternatives removed by fixed transition
  /// ignore/illegal words. This is derived from the v1 schema and therefore
  /// is an implementation detail rather than a serialized identity.
  std::unordered_set<uint64_t> suppressedTransitionAlternatives;
};

struct FunctionalCoverageTypeKey {
  uint64_t typeID = 0;
  obelisk::coverage::Digest configuration{};

  bool operator<(const FunctionalCoverageTypeKey &other) const {
    return std::tie(typeID, configuration) <
           std::tie(other.typeID, other.configuration);
  }
};

/// Mutable type-option state is context-wide and keyed only by stable schema
/// identities. It is deliberately separate from resolved configurations so a
/// procedural assignment updates already-bound configurations and is also
/// inherited by configurations constructed later.
struct FunctionalCoverageTypeOptions {
  std::optional<uint32_t> goal;
  std::optional<uint32_t> weight;
  std::optional<bool> mergeInstances;
  std::map<uint64_t, uint32_t> itemGoals;
  std::map<uint64_t, uint32_t> itemWeights;
  /// Zero names the group; positive keys name template FunctionalItems.
  std::map<uint64_t, std::string> comments;
};

struct FunctionalCoverageInstanceState {
  struct TransitionActiveState {
    uint32_t ordinal = 0;
    uint32_t gapForbiddenOrdinal = UINT32_MAX;
    uint64_t repetitionsConsumed = 0;
    bool excluded = false;

    bool operator<(const TransitionActiveState &other) const {
      return std::tie(ordinal, gapForbiddenOrdinal, repetitionsConsumed,
                      excluded) <
             std::tie(other.ordinal, other.gapForbiddenOrdinal,
                      other.repetitionsConsumed, other.excluded);
    }
  };

  uint64_t typeID = 0;
  obelisk::coverage::Digest configuration{};
  std::string name;
  bool generatedName = false;
  bool enabled = true;
  bool strobe = false;
  uint32_t instanceGoal = 100;
  uint32_t instanceWeight = 1;
  uint64_t groupAtLeast = 1;
  uint64_t groupCrossNumPrintMissing = 0;
  std::vector<uint32_t> itemGoals;
  std::vector<uint32_t> itemWeights;
  /// Effective threshold for every resolved item, including zero-bin items.
  std::vector<uint64_t> itemAtLeast;
  std::unordered_set<uint64_t> explicitAtLeastItems;
  std::unordered_map<uint64_t, uint64_t> crossAtLeast;
  /// Cross print limits use template IDs because procedural selectors and
  /// persisted instance-option owners are stable across resolved schemas.
  std::unordered_map<uint64_t, uint64_t> crossNumPrintMissing;
  std::unordered_set<uint64_t> explicitCrossNumPrintMissingItems;
  /// Mutable comments keyed by zero for the group or by template item ID.
  std::map<uint64_t, std::string> comments;
  std::unordered_set<uint64_t> disabledItems;
  std::vector<FunctionalCoverageValue> formals;
  std::vector<FunctionalCoverageBinState> bins;
  /// Live overlapping matches keyed by resolved transition alternative. A
  /// zero repeat count means the step has not matched yet. For a successor or
  /// terminal accepting gap of nonconsecutive repetition,
  /// gapForbiddenOrdinal identifies the repeated step whose value may not
  /// recur. The terminal accepting state uses ordinal == stepCount.
  std::map<uint64_t, std::set<TransitionActiveState>> transitionActive;
  std::unordered_set<uint64_t> transitionSampledItems;
  std::map<uint64_t,
           std::map<std::vector<uint64_t>, FunctionalCoverageCrossTupleState>>
      crossTuples;
};

struct CoverageToggleBinding {
  uint64_t coverageBase = 0;
  uint64_t stateLow = 0;
  uint64_t bitWidth = 0;
};

struct CovergroupBlockEventRegistration {
  uint64_t id = 0;
  uint64_t covergroupHandle = 0;
  obelisk_rt_object_v1 *receiver = nullptr;
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  uint64_t observerCodeUnitID = 0;
  bool native = false;
  std::vector<obelisk_rt_computed_capture_v1> captures;
  // Construction-time automatic captures outlive the process that invoked
  // new(). Retain them for the context-owned service lifetime; context
  // teardown destroys the complete automatic-state table after this record.
  std::vector<uint64_t> retainedAutomaticCaptures;
};

struct CovergroupBlockEventFeatureState {
  uint64_t nextRegistration = 1;
  std::unordered_map<uint64_t,
                     std::unique_ptr<CovergroupBlockEventRegistration>>
      registrations;
  std::map<std::pair<uint64_t, uint32_t>, std::vector<uint64_t>> buckets;
};

// One context-owned service backs code and functional coverage. The code
// arrays are finalized before worker execution; relaxed atomic line hits then
// avoid the context mutex. Toggle publication already serializes committed
// state transitions and uses the same context lock as the shadow planes.
struct CoverageState {
  std::unique_ptr<obelisk::coverage::Database> schema;
  uint64_t nextInstance = 1;
  std::map<FunctionalCoverageTypeKey, FunctionalCoverageTypeState> types;
  std::map<uint64_t, FunctionalCoverageTypeOptions> typeOptions;
  std::unordered_map<uint64_t, FunctionalCoverageInstanceState> instances;
  std::unique_ptr<std::atomic<uint64_t>[]> lineCounters;
  std::unique_ptr<std::atomic<uint8_t>[]> lineOverflow;
  std::unique_ptr<std::atomic<uint8_t>[]> lineEnabled;
  std::vector<uint8_t> lineExcluded;
  uint64_t lineCount = 0;
  std::vector<uint64_t> toggleCounters;
  std::vector<uint8_t> toggleOverflow;
  std::vector<uint8_t> toggleEnabled;
  std::vector<uint8_t> toggleValue;
  std::vector<uint8_t> toggleUnknown;
  std::vector<uint8_t> toggleExcluded;
  std::vector<uint8_t> toggleBound;
  std::unordered_map<uint32_t, std::vector<CoverageToggleBinding>>
      toggleBindings;
  std::unordered_map<uint64_t, uint64_t> scopeParents;
  std::unordered_map<std::string, std::vector<uint64_t>> definitionScopes;
  std::unique_ptr<CovergroupBlockEventFeatureState> blockEvents;
  uint64_t toggleBitCount = 0;
  bool finalized = false;
  bool toggleBindingsSealed = false;
  std::string outputPath = "coverage.obcov";
  std::string testName;
  std::vector<std::pair<std::string, std::string>> tags;
  std::vector<std::string> loadPaths;
  uint64_t persistedRunFlags = 0;
  bool dumpSuppressed = false;
  bool outputExplicit = false;
  obelisk::coverage::UUID runUUID{};
};

// Query immutable resolved covergroup type policy while the context mutex is
// held. This is internal runtime state, not a second public ABI surface.
obelisk_rt_status
obelisk_rt_covergroup_strobe_unlocked(obelisk_rt_context *context,
                                      uint64_t handle, bool &strobe);

/// Record a committed canonical state transition. The caller owns the context
/// mutex. Covered aliases are intentionally represented by separate bindings.
void obelisk_rt_coverage_record_transition_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *newValue, const uint8_t *newUnknown);

/// Return whether a registered static state contributes any toggle
/// obligations. The caller owns the context mutex.
bool obelisk_rt_coverage_tracks_static_state_unlocked(
    const obelisk_rt_context *context, uint32_t staticID);

struct SampledHistoryState {
  uint64_t bitWidth = 0;
  uint64_t depth = 0;
  uint64_t count = 0;
  uint64_t next = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

struct StochasticQueueEntry {
  uint32_t jobValue = 0;
  uint32_t jobUnknown = 0;
  uint32_t informValue = 0;
  uint32_t informUnknown = 0;
  uint64_t addTime = 0;
};

struct StochasticQueueState {
  uint32_t type = 0;
  uint64_t maximumLength = 0;
  uint64_t head = 0;
  uint64_t count = 0;
  uint64_t maximumCount = 0;
  uint64_t numberOfAdds = 0;
  uint64_t firstAddTime = 0;
  uint64_t latestAddTime = 0;
  uint64_t shortestWait = UINT64_MAX;
  bool haveShortestWait = false;
  unsigned __int128 completedWait = 0;
  std::vector<StochasticQueueEntry> entries;
};

// Compact prefix trie for Clause 21.6 plusargs. Nodes and edges live in two
// contiguous vectors so startup performs no allocation per character. Every
// node records the earliest argv entry reaching that prefix, which makes both
// plusarg queries O(prefix length) without losing command-line order.
struct PlusargIndexNode {
  uint32_t firstArgument = UINT32_MAX;
  uint32_t firstEdge = UINT32_MAX;
};

struct PlusargIndexEdge {
  uint32_t nextNode = UINT32_MAX;
  uint32_t nextEdge = UINT32_MAX;
  uint8_t character = 0;
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
  // Full invocation arguments are retained only for a VPI-readable design.
  // They are never consulted by execution or the scheduler; vpi_get_vlog_info
  // snapshots them on explicit request.
  std::vector<std::string> vpiArguments;
  std::vector<PlusargIndexNode> plusargIndexNodes;
  std::vector<PlusargIndexEdge> plusargIndexEdges;
  bool plusargIndexBuilt = false;
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
  // Clause 20.16 queues are design-global and keyed by their explicit q_id.
  // Ordinary designs pay only for the empty map; successful initialization
  // allocates the fixed-capacity ring once, so add/remove remain O(1).
  std::unordered_map<int32_t, StochasticQueueState> stochasticQueues;
  std::shared_ptr<const uint8_t> errorLifetime;
  std::vector<ScheduledProcess> scheduledProcesses;
  const obelisk_rt_native_schedule_plan *nativeSchedulePlan = nullptr;
  std::vector<obelisk::runtime::EvalNBAQueue *> nativeEvalNBAQueues;
  uint8_t *nativeStateValue = nullptr;
  uint8_t *nativeStateUnknown = nullptr;
  uint64_t nativeStateBitCount = 0;
  // Addressing permission for native fragments using the generic scheduler.
  // This does not authorize static fanout, NBA elision, or two-state execution.
  uint32_t *nativeStateSpecializationFast = nullptr;
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
  std::vector<uint32_t> nativeScheduleNBARootIndex;
  std::vector<obelisk_rt_process_instance_v1 *> nativeScheduleActors;
  std::vector<uint64_t> nativeScheduleActorTokens;
  std::vector<size_t> nativeScheduleActorIndices;
  std::vector<obelisk_rt_native_schedule_node> nativeScheduleNodes;
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>>
      nativeScheduleActorNodes;
  obelisk::runtime::CursorReadySet nativeScheduleReadyNodes;
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
  bool prioritySignalPending = false;
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
  // Set only while a live VPI registration can observe running design state.
  // Static startup inspection leaves this false, so loading an otherwise
  // inert read-only VPI library does not perturb the Tier-1 hot path.
  bool vpiObservationDemand = false;
  // A static eval island may use exact fanout only after periodic preparation
  // has proved that no runtime Clause 31 primary is generated-writable.
  bool nativeStaticEvalIslandCertified = false;
  bool nativeScheduleDesignTaskFilterActive = false;
  // A rejected slow-dominant bytecode shape stays on the exact scanner until
  // a structural invalidation. Keep this beside the native filter booleans so
  // the hot dispatcher needs one byte test rather than re-reading the cold
  // cohort object on every arbitration.
  bool designReadyCohortExactScan = false;
  uint64_t nativeScheduleForcedDesignTask = 0;
  uint64_t nativePeriodicRuntimeDeadline = UINT64_MAX;
  std::vector<uint32_t> nativePeriodicClockActorSlots;
  // Derived solely from immutable installed-plan tables. Live subscriptions
  // and external overrides are still checked at every periodic re-entry.
  std::unordered_set<uint32_t> nativePeriodicGeneratedWritableStates;
  std::unordered_map<uint64_t, size_t> scheduledProcessIndices;
  std::unordered_set<uint64_t> nativePollCandidates;
  NativeReadyPublicationBatch *nativeReadyPublicationBatch = nullptr;
  // Lazy min-heap of (wake time, process token). Stale entries are discarded
  // when queried after a process resumes, changes wait kind, or terminates.
  std::vector<std::pair<uint64_t, uint64_t>> scheduledProcessDelayHeap;
  bool scheduledFinalProcessPresent = false;
  std::unordered_set<uint64_t> unstartedActiveActors;
  std::unordered_set<uint64_t> unstartedFinalActors;
  std::unordered_map<uint64_t, SignalValueSnapshot> signalValueSnapshots;
  std::unordered_map<SignalSubscriptionBucketKey,
                     std::vector<SignalSubscriptionBucketEntry>,
                     SignalSubscriptionBucketKeyHash>
      signalSubscriptionBuckets;
  // Dynamic native subscriptions are omitted from the frozen AOT fanout.
  // Computed waits and custom Clause 31.5 descriptors share this already-hot
  // specialization guard instead of adding a transition-path feature probe.
  uint64_t nativeDynamicSignalSubscriptions = 0;
  // Immutable observer descriptors are executable code inventory, not live
  // wakeups. Only a registered suspend.observe wait invalidates generated AOT
  // closure. Owner-local registration bits make this an allocation-free O(1)
  // count; synchronous timing-condition evaluators never increment it.
  uint64_t activeComputedObserverWaiterCount = 0;
  std::vector<uint64_t> pendingNativeComputedWaiters;
  std::vector<uint64_t> pendingDesignComputedWaiters;
  std::unordered_set<uint64_t> nativeConditionalSignalWaiters;
  std::unordered_set<uint64_t> designConditionalSignalWaiters;
  uint64_t schedulerSelectionGeneration = 1;
  bool signalDiagnosticsEnabled = false;
  bool signalDiagnosticsReport = false;
  SignalSubscriptionDiagnostics signalDiagnostics;
  std::vector<ScheduledNBA> scheduledNBAs;
  // Path-delay events have one live entry per destination bit. Keeping them
  // in an ordered keyed calendar lets pulse rejection remove a superseded
  // long-delay event directly instead of leaving a tombstone in the generic
  // NBA vector for every transition.
  std::map<std::pair<uint64_t, uint64_t>, ScheduledNBA>
      scheduledInertialPathNBAs;
  std::unordered_map<InertialDriverSite, InertialDriverPending,
                     InertialDriverSiteHash>
      inertialDriverPending;
  std::unordered_map<InertialDriverSite, InertialPathPending,
                     InertialDriverSiteHash>
      inertialPathPending;
  std::unordered_map<InertialDriverSite, InertialStrengthPathPending,
                     InertialDriverSiteHash>
      inertialStrengthPathPending;
  std::unordered_map<uint64_t, InertialNetPending> inertialNetPending;
  bool schedulerApplyingNativeUpdate = false;
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
  ScheduledPassSwitchEvents scheduledPassSwitchEvents;
  std::unordered_map<uint32_t, DelayedPassSwitchPending>
      delayedPassSwitchPending;
  std::unordered_map<uint64_t, DelayedMosPending> delayedMosPending;
  std::unordered_map<uint32_t, DelayedMosControl> delayedMosControls;
  std::unordered_map<uint64_t, uint16_t> delayedMosContributions;
  std::vector<ScheduledDesignTask> scheduledDesignTasks;
  std::unordered_map<uint64_t, size_t> scheduledDesignTaskIndices;
  std::unordered_set<uint64_t> designPollCandidates;
  ReusableByteBufferPool designTaskFrames;
  uint64_t nextSchedulerSequence = 1;
  uint64_t nextDynamicEventID = 1;
  uint64_t nextNativeProcessToken = 1;
  uint32_t nextNativeAutomaticID = 1;
  uint64_t nextDesignTaskID = 1;
  uint64_t nextProcessInsertionSequence = 1;
  uint64_t nextWaitSequence = 1;
  uint64_t activeDesignTaskID = 0;
  ScheduledDesignTask *activeDesignTask = nullptr;
  uint32_t activeDesignTaskPhase = 0;
  bool activeWaitOrderFailed = false;
  uint32_t activeHomeRegion = UINT32_MAX;
  uint32_t activeExecRegion = UINT32_MAX;
  uint64_t activeLogicalProcessToken = 0;
  uint64_t activeProgramOwner = 0;
  bool controlEscapePending = false;
  // Cold Clause 35.9 bookkeeping for the one exported task currently being
  // serviced by a nested scheduler. Ordinary scheduler dispatch never reads
  // these fields.
  uint64_t activeDpiExportTaskLogical = 0;
  bool activeDpiExportTaskDisabled = false;
  // Bytecode tasks are removed from the scheduler vector while executing.
  // Preserve their logical parent so ancestor-directed process control can
  // still identify that the active activation belongs to the target tree.
  uint64_t activeLogicalProcessParent = 0;
  // Process tokens are never reused. Remember live logical processes known to
  // have owned a child so the overwhelmingly common childless termination can
  // skip a full native/design task scan. Natural termination erases the mark;
  // any conservative stale-live mark only causes the old exact scan.
  std::unordered_set<uint64_t> logicalProcessParentsWithChildren;
  // A bytecode design task is moved out of the scheduler vector while it
  // executes, so its lane-local stream cannot be rediscovered by token.
  obelisk_rt_random_state_v1 *activeRandom = nullptr;
  uint64_t monitorLogicalProcessToken = 0;
  bool monitorEnabled = true;
  // What the registered monitor reported last. A monitor process wakes on any
  // change to a variable it reads, but IEEE 1800-2017 21.2.3 reports only when
  // one of its own arguments changes value.
  std::string monitorReport;
  bool monitorReported = false;
  std::vector<uint64_t> activeControls;
  obelisk_rt_process_instance_v1 *activeNativeProcess = nullptr;
  bool designTaskExecuting = false;
  bool schedulerCompactionPending = false;
  size_t schedulerDeadProcessCount = 0;
  size_t schedulerDeadDesignTaskCount = 0;
  TerminatedTokenSet terminatedDesignTasks;
  TerminatedTokenSet terminatedNativeProcesses;
  // Forced cancellation is a terminal process::KILLED state, while ordinary
  // completion remains process::FINISHED. These sets are subsets of the two
  // terminated sets above so await/join readiness stays reason-independent.
  TerminatedTokenSet killedDesignTasks;
  TerminatedTokenSet killedNativeProcesses;
  uint64_t nextControlActivation = 1;
  std::unordered_map<uint64_t, ControlActivation> controlActivations;
  std::unordered_set<uint64_t> initializedStaticSites;
  uint64_t deferredImmediateTime = UINT64_MAX;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      deferredImmediateSites;
  struct DeferredImmediateReport {
    uint64_t logicalProcess = 0;
    uint64_t assertion = 0;
  };
  uint64_t nextDeferredImmediateTicket = 1;
  std::unordered_map<uint64_t, DeferredImmediateReport>
      deferredImmediateReports;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      deferredImmediateProcessReports;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
      deferredImmediateAssertionReports;
  // Per-identity nondefault IEEE assertion-control bits. See Runtime.cpp for
  // the compact bit layout; absent entries have the all-enabled defaults.
  std::unordered_map<uint64_t, uint8_t> assertionControlStates;
  // Per-identity generation advanced by every effective Kill. Concurrent
  // monitors and queued reports snapshot this value so Kill can invalidate
  // live work without exposing compiler-owned state to the runtime.
  std::unordered_map<uint64_t, uint64_t> assertionKillEpochs;
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
  // Set once a computed observer registers the compiler-reserved Preponed
  // event dependency. It remains sticky because those detached observers are
  // design-lifetime services and the branch keeps unrelated designs free of
  // per-slot observer scans.
  bool preponedObserverPresent = false;
  uint64_t schedulerSlotProgress = 0;
  bool schedulerRunningFinals = false;
  bool schedulerFinishRequested = false;
  // An explicit termination request made while a final procedure is active
  // suppresses every remaining final procedure (IEEE 1800-2017 9.2.3).
  bool schedulerFinalsAborted = false;
  // Program completion is event-driven: each owned logical process is
  // registered once and removed once, so ordinary scheduler selection never
  // scans for 24.7 completion.
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> programProcesses;
  uint64_t liveProgramInstances = 0;
  bool programTrackingSeen = false;
  bool programTrackingSealed = false;
  // Mirrored as a full word for generated code. The address is handed out
  // only while the native scheduler owns the context transaction.
  uint32_t nativePeriodicTerminationRequested = 0;
  uint32_t schedulerFinishVerbosity = 0;
  obelisk_rt_status schedulerFinishStatus = OBELISK_RT_OK;
  obelisk_rt_status schedulerStatus = OBELISK_RT_OK;
  uint32_t observerDepth = 0;
  bool observerForcesCanonicalPlane = false;
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  // Live simulation state is owned by the context.  The planes use the same
  // little-endian limb representation as bytecode values and are never stored
  // in the immutable reflection image.
  std::vector<uint64_t> stateValue;
  std::vector<uint64_t> stateUnknown;
  // Canonical state captured once at entry to each time slot, before any
  // Active-region work. Sampled-value reads never consult the live planes.
  std::vector<uint64_t> preponedValue;
  std::vector<uint64_t> preponedUnknown;
  std::unordered_map<uint64_t,
                     std::unordered_map<uint64_t, SampledHistoryState>>
      sampledHistories;
  std::unordered_map<uint64_t, SampledHistoryState> clockedSampleHistories;
  // Language and VPI force state is allocated on first use. A set bit masks
  // every ordinary publication to the corresponding canonical design bit.
  std::vector<uint64_t> forceMask;
  std::vector<uint64_t> assignMask;
  std::vector<uint64_t> assignValue;
  std::vector<uint64_t> assignUnknown;
  // A nonzero entry identifies the detached evaluator that owns a dynamic
  // force/procedural-assign bit. Sparse ownership keeps ordinary designs free
  // of per-bit allocation and lets partially overlapping overrides retire an
  // evaluator exactly when its final bit is superseded or released.
  std::unordered_map<uint64_t, uint64_t> dynamicForceOwners;
  std::unordered_map<uint64_t, uint64_t> dynamicAssignOwners;
  struct ManagedOverrideState {
    uint64_t planeSize = 0;
    bool fourState = false;
    bool forceActive = false;
    bool assignActive = false;
    uint64_t forceOwner = 0;
    uint64_t assignOwner = 0;
    std::vector<uint8_t> forceValue;
    std::vector<uint8_t> forceUnknown;
    std::vector<uint8_t> assignValue;
    std::vector<uint8_t> assignUnknown;
    // Shadow storage is outside the managed heap. Keep every validated
    // referent precise and live while its layer remains active, including a
    // lower-priority procedural assign hidden beneath force.
    std::vector<obelisk_rt_object_v1 *> forceRoots;
    std::vector<obelisk_rt_object_v1 *> assignRoots;
  };
  // Class-property overrides are keyed by monotonic object identity and byte
  // offset. Variable selects are not legal force targets, so one exact field
  // slot is sufficient and avoids per-bit maps on the managed heap hot path.
  std::unordered_map<uint64_t,
                     std::unordered_map<uint64_t, ManagedOverrideState>>
      managedOverrides;
  // Keeps ordinary managed-container mutation to one relaxed load until a
  // design actually executes force or procedural assign. This is sticky:
  // those statements are rare, and clearing it is not needed for correctness.
  std::atomic<bool> managedValueOverridePossible{false};
  // Latest values published by continuous assignments to variable storage.
  // Unlike procedural writes, these remain active beneath force / assign and
  // are republished as soon as the higher-priority override is released.
  std::vector<uint64_t> continuousMask;
  std::vector<uint64_t> continuousValue;
  std::vector<uint64_t> continuousUnknown;
  // Built once from the immutable execution image and shared by net
  // resolution, force/release, deposits, and reflection connectivity checks.
  NetAliasCache netAliases;
  DesignDatabaseCache designDatabase;
  bool designDatabaseRegistered = false;
  obelisk::designbytecode::Image designBytecodeImage;
  bool designBytecodeImageValidated = false;
  void *vpiState = nullptr;
  // Waveform dump plan, shadow planes, and output buffer. Allocated on the
  // first $dumpfile/$dumpvars and owned by the context.
  VCDTraceState *vcdState = nullptr;
  EVCDTraceState *evcdState = nullptr;
  std::unordered_map<uint64_t, const obelisk_rt_class_descriptor_v1 *>
      managedClasses;
  std::unordered_map<uint64_t, const obelisk_rt_element_type_v1 *>
      managedElementTypes;
  std::unordered_map<uint64_t, std::unique_ptr<OwnedElementTypeDescriptor>>
      managedOwnedElementTypes;
  // Object identities are monotonic for the lifetime of a managed heap.
  // Each observed field/size selector therefore receives one stable scheduler
  // token without retaining the object itself.
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>>
      managedWatchTokens;
  // Direct change waits index managed-watch tokens separately from packed
  // signal ranges. This keeps mutation wakeup O(number of interested waiters)
  // without inventing a colliding stable-signal handle namespace.
  std::unordered_map<uint64_t, std::unordered_set<SignalSubscription *>>
      managedWatchWaiters;
  uint64_t nextManagedWatchToken = 1;
  std::unique_ptr<CoverageState> coverage;
  std::vector<obelisk_rt_process_instance_v1 *> managedRootProcesses;
  ManagedHeap *managedHeap = nullptr;
  obelisk_rt_random_state_v1 random{};
  uint64_t configuredSeed = 1;
  // IEEE 1800 Annex N state for the no-argument `$random` form. Keep this
  // independent of hierarchical `$urandom` streams and initialize it to the
  // standardized algorithm's zero-seed entry point.
  int32_t legacyRandomSeed = 0;
  // Cold feature-local tail: bounded parsed plans for dynamic
  // $sscanf/$fscanf format strings. Keeping this after all preexisting fields
  // preserves their offsets; null is the complete no-feature state.
  DynamicScanState *dynamicScanState = nullptr;

  // Cold, pay-for-play exact multi-clock assertion state. Keep this pointer at
  // the tail so every preexisting context field retains its offset, and leave
  // it null for designs that never execute a clock-cohort wait.
  std::unique_ptr<ClockOccurrenceFeatureState> clockOccurrences;
  std::unique_ptr<CovergroupClockEventFeatureState> covergroupClockEvents;
  // Compiler-generated primary and iff observers are read-only. This guard
  // rejects malformed descriptors that publish while event-instant history is
  // being committed; it is cleared before user sample evaluators run.
  uint32_t covergroupClockEventEvaluationDepth = 0;
  // Scoped to synchronous computed-condition and covergroup sampler
  // evaluation. Keeping the publication view on the context allows the two
  // independent lazy features to share exact post-transition loads.
  const ClockConditionPublicationView *conditionPublication = nullptr;
  // IEEE 1800-2017 31.4.6 alone needs retroactive/deferred occurrence
  // storage. Keep the entire map pointer-lazy and outside the ordinary clock
  // feature so every other assertion/timing wait retains its exact layout.
  std::unique_ptr<NoChangeFeatureState> noChangeChecks;

  // Cold, pay-for-play acceleration for a large bytecode direct-signal ready
  // cohort. Null preserves ordinary native/generic/AOT allocation behavior;
  // tail placement preserves every preexisting context field offset.
  std::unique_ptr<DesignReadyCohortState> designReadyCohort;

  RecursiveWatchGroupState *recursiveWatchGroups = nullptr;
  ClassBitstreamState *classBitstreamState = nullptr;

  // A cached boundary deadline costs the same test with VPI off or dormant.
  // The ordered callback records remain in the lazily allocated VPI state.
  std::optional<uint64_t> nextVPITimeCallback;
  bool vpiTimeCallbackActive = false;

  obelisk_rt_context();
  ~obelisk_rt_context();
};

// Execute due Pre-Active foreign work; never advance time or drain actors.
obelisk_rt_status
obelisk_rt_vpi_dispatch_time_callbacks_unlocked(obelisk_rt_context *context);

inline ReplaceableEventFeatureState *
obelisk_rt_replaceable_events(obelisk_rt_context *context) {
  return context && context->clockOccurrences
             ? context->clockOccurrences->replaceableEvents.get()
             : nullptr;
}

inline const ReplaceableEventFeatureState *
obelisk_rt_replaceable_events(const obelisk_rt_context *context) {
  return context && context->clockOccurrences
             ? context->clockOccurrences->replaceableEvents.get()
             : nullptr;
}

inline void
obelisk_rt_invalidate_design_ready_cohort(obelisk_rt_context *context) {
  if (!context)
    return;
  context->designReadyCohortExactScan = false;
  if (!context->designReadyCohort)
    return;
  context->designReadyCohort->valid = false;
  context->designReadyCohort->suppressed = false;
  context->designReadyCohort->persistentSuppression = false;
}

// Caller holds the context lock and handles allocation failures. Only a new
// direct native signal candidate is admissible. Every other mutation keeps
// the ordinary generation invalidation, so gaps cannot be mistaken for wakes.
inline void obelisk_rt_record_native_ready_publication_unlocked(
    obelisk_rt_context *context, uint64_t token, bool inserted) {
  uint64_t previous = context->schedulerSelectionGeneration;
  if (++context->schedulerSelectionGeneration == 0)
    context->schedulerSelectionGeneration = 1;
  auto *batch = context->nativeReadyPublicationBatch;
  if (!batch || !batch->valid)
    return;
  if (!inserted || batch->generation != previous ||
      context->schedulerSelectionGeneration == 1 || batch->queued.count(token)) {
    batch->valid = false;
    return;
  }
  batch->tokens.push_back(token);
  batch->generation = context->schedulerSelectionGeneration;
}

inline void
obelisk_rt_set_design_task_filter_unlocked(obelisk_rt_context *context,
                                           bool active, uint64_t forcedTask) {
  if (!context)
    return;
  if (context->nativeScheduleDesignTaskFilterActive != active ||
      context->nativeScheduleForcedDesignTask != forcedTask)
    obelisk_rt_invalidate_design_ready_cohort(context);
  context->nativeScheduleDesignTaskFilterActive = active;
  context->nativeScheduleForcedDesignTask = forcedTask;
}

void obelisk_rt_sync_native_state_range_unlocked(obelisk_rt_context *context,
                                                 uint64_t begin,
                                                 uint64_t width);

obelisk_rt_status
obelisk_rt_capture_preponed_unlocked(obelisk_rt_context *context);

// Emit every waveform value that changed during the time slot that is about to
// end. Called immediately before each scheduler time advance and once more when
// the run terminates, so a slot is recorded exactly once at its own time. This
// is a no-op when no dump file is open.
obelisk_rt_status obelisk_rt_dump_slot_unlocked(obelisk_rt_context *context);

// Nonzero while a dump file is open. Execution tiers that advance time without
// re-entering the runtime must not be selected while this holds.
bool obelisk_rt_dump_active_unlocked(const obelisk_rt_context *context);

// Number of distinct canonical ranges the dump differences per time slot.
// Aliased declarations collapse onto one range, so this is at most the number
// of traced `$var` declarations.
uint64_t obelisk_rt_dump_traced_range_count(const obelisk_rt_context *context);

// Flush and release the dump state during context teardown.
void obelisk_rt_dump_destroy(obelisk_rt_context *context) noexcept;

inline std::unordered_set<uint64_t> &
obelisk_rt_unstarted_actors(obelisk_rt_context *context, uint32_t phase) {
  return phase == 0 ? context->unstartedActiveActors
                    : context->unstartedFinalActors;
}

inline void obelisk_rt_register_unstarted_actor(obelisk_rt_context *context,
                                                uint32_t phase,
                                                uint64_t logicalToken) {
  obelisk_rt_unstarted_actors(context, phase).insert(logicalToken);
}

inline void obelisk_rt_unregister_unstarted_actor(obelisk_rt_context *context,
                                                  uint32_t phase,
                                                  uint64_t logicalToken) {
  obelisk_rt_unstarted_actors(context, phase).erase(logicalToken);
}

inline bool
obelisk_rt_logical_process_terminated(const obelisk_rt_context *context,
                                      uint64_t logicalToken) {
  if (!context || logicalToken == 0)
    return false;
  if ((logicalToken & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0)
    return context->terminatedNativeProcesses.count(
               logicalToken & ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0;
  return context->terminatedDesignTasks.count(logicalToken) != 0;
}

// The earliest home region holding an actor that has never run, or UINT32_MAX
// when every actor of this phase has started. A never-started actor takes its
// first activation ahead of signal resumptions in its own region, but it must
// not hold back an earlier one: a postponed $monitor actor that preempted the
// active region would observe the values from before that region ran, and the
// Postponed region would then be reached twice in one time slot
// (IEEE 1800-2017 4.4.2.9).
inline uint32_t obelisk_rt_unstarted_actor_region(obelisk_rt_context *context,
                                                  uint32_t phase) {
  auto &actors = obelisk_rt_unstarted_actors(context, phase);
  uint32_t earliest = UINT32_MAX;
  for (auto actor = actors.begin(); actor != actors.end();) {
    uint64_t logicalToken = *actor;
    bool pending = false;
    bool explicitlySuspended = false;
    uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
    if ((logicalToken & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
      uint64_t token = logicalToken & ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
      auto indexed = context->scheduledProcessIndices.find(token);
      if (indexed != context->scheduledProcessIndices.end() &&
          indexed->second < context->scheduledProcesses.size()) {
        const ScheduledProcess &process =
            context->scheduledProcesses[indexed->second];
        pending = process.instance && process.token == token &&
                  process.phase == phase && !process.started;
        explicitlySuspended = process.explicitlySuspended;
        homeRegion = process.homeRegion;
      }
    } else {
      auto indexed = context->scheduledDesignTaskIndices.find(logicalToken);
      if (indexed != context->scheduledDesignTaskIndices.end() &&
          indexed->second < context->scheduledDesignTasks.size()) {
        const ScheduledDesignTask &task =
            context->scheduledDesignTasks[indexed->second];
        pending = !task.terminated && task.id == logicalToken &&
                  task.phase == phase && !task.started;
        explicitlySuspended = task.explicitlySuspended;
        homeRegion = task.homeRegion;
      }
    }
    if (!pending) {
      actor = actors.erase(actor);
      continue;
    }
    // An externally suspended actor remains live and unstarted, but it must
    // not hold back runnable actors in its home region. Keep its token in the
    // inventory so clearing explicit suspension restores bootstrap ordering.
    if (explicitlySuspended) {
      ++actor;
      continue;
    }
    // Nothing can precede the active region, so stop compacting there.
    if (homeRegion == OBELISK_RT_REGION_ACTIVE)
      return OBELISK_RT_REGION_ACTIVE;
    earliest = std::min(earliest, homeRegion);
    ++actor;
  }
  return earliest;
}

// Read-only counterpart used by inspection APIs. Unlike the scheduler helper,
// this never compacts stale inventory entries.
inline uint32_t
obelisk_rt_peek_unstarted_actor_region(const obelisk_rt_context *context,
                                       uint32_t phase) {
  const auto &actors = phase == 0 ? context->unstartedActiveActors
                                  : context->unstartedFinalActors;
  uint32_t earliest = UINT32_MAX;
  for (uint64_t logicalToken : actors) {
    bool pending = false;
    bool explicitlySuspended = false;
    uint32_t homeRegion = OBELISK_RT_REGION_ACTIVE;
    if ((logicalToken & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
      uint64_t token = logicalToken & ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
      auto indexed = context->scheduledProcessIndices.find(token);
      if (indexed != context->scheduledProcessIndices.end() &&
          indexed->second < context->scheduledProcesses.size()) {
        const ScheduledProcess &process =
            context->scheduledProcesses[indexed->second];
        pending = process.instance && process.token == token &&
                  process.phase == phase && !process.started;
        explicitlySuspended = process.explicitlySuspended;
        homeRegion = process.homeRegion;
      }
    } else {
      auto indexed = context->scheduledDesignTaskIndices.find(logicalToken);
      if (indexed != context->scheduledDesignTaskIndices.end() &&
          indexed->second < context->scheduledDesignTasks.size()) {
        const ScheduledDesignTask &task =
            context->scheduledDesignTasks[indexed->second];
        pending = !task.terminated && task.id == logicalToken &&
                  task.phase == phase && !task.started;
        explicitlySuspended = task.explicitlySuspended;
        homeRegion = task.homeRegion;
      }
    }
    if (!pending || explicitlySuspended)
      continue;
    if (homeRegion == OBELISK_RT_REGION_ACTIVE)
      return OBELISK_RT_REGION_ACTIVE;
    earliest = std::min(earliest, homeRegion);
  }
  return earliest;
}

inline bool obelisk_rt_unstarted_actor_pending(obelisk_rt_context *context,
                                               uint32_t phase) {
  // Keep the steady-state empty inventory check inline. Region selection also
  // compacts stale startup entries and is intentionally a much larger helper.
  return !obelisk_rt_unstarted_actors(context, phase).empty() &&
         obelisk_rt_unstarted_actor_region(context, phase) != UINT32_MAX;
}

inline bool
obelisk_rt_has_conditional_signal_waiters(const obelisk_rt_context *context) {
  return context && (!context->nativeConditionalSignalWaiters.empty() ||
                     !context->designConditionalSignalWaiters.empty() ||
                     (context->clockOccurrences &&
                      context->clockOccurrences->conditionalWaitCount != 0));
}

inline bool obelisk_rt_is_clock_occurrence_wait_flags(uint32_t flags) {
  return (flags & ~(OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                    OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS)) ==
         OBELISK_RT_WAIT_CLOCK_OCCURRENCE;
}

inline bool
obelisk_rt_is_slot_final_clock_occurrence_wait_flags(uint32_t flags) {
  return obelisk_rt_is_clock_occurrence_wait_flags(flags) &&
         (flags & OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL) != 0;
}

inline bool obelisk_rt_is_slot_final_clock_occurrence_wait(
    const obelisk_rt_wait_record_v1 *wait) {
  return wait &&
         obelisk_rt_is_slot_final_clock_occurrence_wait_flags(wait->flags);
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
        wait->flags == OBELISK_RT_WAIT_EDGE_IFF ||
        obelisk_rt_is_clock_occurrence_wait_flags(wait->flags))
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
struct ManagedObjectLease;
OBELISK_RT_FEATURE_HELPER void
obelisk_rt_managed_object_release(ManagedObjectLease *lease) noexcept;
struct ManagedObjectLease {
  void *metadata = nullptr;
  uint8_t *object = nullptr;
  uint64_t extent = 0;
  uint32_t ticket = 0;

  ManagedObjectLease() = default;
  ManagedObjectLease(const ManagedObjectLease &) = delete;
  ManagedObjectLease &operator=(const ManagedObjectLease &) = delete;
  ManagedObjectLease(ManagedObjectLease &&other) noexcept {
    *this = std::move(other);
  }
  ManagedObjectLease &operator=(ManagedObjectLease &&other) noexcept {
    if (this != &other) {
      obelisk_rt_managed_object_release(this);
      metadata = std::exchange(other.metadata, nullptr);
      object = std::exchange(other.object, nullptr);
      extent = std::exchange(other.extent, 0);
      ticket = std::exchange(other.ticket, 0);
    }
    return *this;
  }
  ~ManagedObjectLease() { obelisk_rt_managed_object_release(this); }
};
obelisk_rt_status obelisk_rt_managed_allocate(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_managed_kind_v1 kind,
                                              uint64_t extent,
                                              uint64_t alignment,
                                              const void *runtimeDescriptor,
                                              obelisk_rt_object_v1 **outObject);
// Allocate while the calling lane remains active without entering a
// safepoint. This is reserved for publishing allocation-backed caches from
// operations whose inputs are not compiler-visible GC roots. The allocation
// still uses the ordinary allocator and updates all accounting; a subsequent
// ordinary allocation observes the collection threshold.
OBELISK_RT_FEATURE_HELPER obelisk_rt_status
obelisk_rt_managed_allocate_without_safepoint(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_managed_kind_v1 kind,
                                              uint64_t extent,
                                              uint64_t alignment,
                                              const void *runtimeDescriptor,
                                              obelisk_rt_object_v1 **outObject);
obelisk_rt_status
obelisk_rt_managed_object_access(obelisk_rt_object_v1 *object,
                                 obelisk_rt_managed_kind_v1 expectedKind,
                                 ManagedObjectAccess access, void *environment);
// Feature-local zero-copy traversal can retain a managed object's ticket lock
// across an explicit continuation frame. Every successful acquire must be
// paired with exactly one release; acquiring a second lease for the same
// object before releasing the first is invalid.
OBELISK_RT_FEATURE_HELPER obelisk_rt_status obelisk_rt_managed_object_acquire(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *object,
    obelisk_rt_managed_kind_v1 expectedKind, ManagedObjectLease *outLease);
obelisk_rt_managed_kind_v1
obelisk_rt_managed_object_kind(const obelisk_rt_object_v1 *object) noexcept;
uint64_t
obelisk_rt_managed_object_extent(const obelisk_rt_object_v1 *object) noexcept;
obelisk_rt_context *
obelisk_rt_managed_object_context(const obelisk_rt_object_v1 *object) noexcept;
const obelisk_rt_class_descriptor_v1 *
obelisk_rt_managed_object_class_descriptor(
    const obelisk_rt_object_v1 *object) noexcept;
uint64_t obelisk_rt_managed_watch_range(obelisk_rt_object_v1 *object,
                                        uint64_t offset,
                                        uint64_t size) noexcept;
void obelisk_rt_managed_trace_runtime_object(obelisk_rt_managed_kind_v1 kind,
                                             uint8_t *object, uint64_t extent,
                                             ManagedTraceVisit visit,
                                             void *environment) noexcept;
// A whole dynamic aggregate selected by force / procedural assign is a value,
// not a mutable reference. Suppress mutations through that selected value
// while its owning override layer is active.
bool obelisk_rt_managed_value_mutation_masked(
    obelisk_rt_object_v1 *object) noexcept;
obelisk_rt_status obelisk_rt_reference_path_shape(obelisk_rt_object_v1 *path,
                                                  uint64_t valueSize,
                                                  uint64_t bitWidth,
                                                  uint32_t fourState,
                                                  uint32_t managedValue);
obelisk_rt_status obelisk_rt_reference_path_element(
    obelisk_rt_object_v1 *path, const obelisk_rt_element_type_v1 **outElement);
void obelisk_rt_enumerate_design_managed_roots(
    obelisk_rt_context *context, ManagedRootVisit visit,
    void *visitorEnvironment) noexcept;
obelisk_rt_status
obelisk_rt_validate_string(obelisk_rt_context *context,
                           obelisk_rt_string_v1 string) noexcept;
obelisk_rt_status obelisk_rt_class_bitstream_export_bytecode(
    obelisk_rt_context *context, uint32_t function, uint32_t site,
    const void *input_value, const void *input_unknown,
    uint64_t input_plane_size, uint64_t input_bit_width,
    uint32_t input_four_state, void *out_value, void *out_unknown,
    uint64_t output_plane_size, uint64_t output_bit_width,
    uint32_t output_four_state, uint32_t observe, uint32_t *out_matched,
    uint64_t *out_watch);

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
/// Latch a nonterminating simulation error while the context mutex is held.
/// The scheduler continues running, but orderly shutdown reports failure.
void latchSchedulerErrorUnlocked(obelisk_rt_context *context) noexcept;
void setLastError(obelisk_rt_context *context, std::string message);
void obelisk_rt_report_signal_diagnostics_unlocked(obelisk_rt_context *context);
void obelisk_rt_release_native_schedule_plan(
    obelisk_rt_context *context) noexcept;
void obelisk_rt_release_eval_nba_queues(obelisk_rt_context *context) noexcept;
void obelisk_rt_aot_external_write_unlocked(obelisk_rt_context *context);
void obelisk_rt_aot_observation_demand_changed_unlocked(
    obelisk_rt_context *context, bool active);
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

void obelisk_rt_program_register_unlocked(obelisk_rt_context *context,
                                          uint64_t logicalProcess,
                                          uint64_t programOwner);
void obelisk_rt_program_complete_unlocked(obelisk_rt_context *context,
                                          uint64_t logicalProcess,
                                          uint64_t programOwner);
void obelisk_rt_program_seal_unlocked(obelisk_rt_context *context);

// A join_none branch may finish while processes spawned beneath it remain
// live. Keep those descendants reachable from the surviving process tree so
// a later wait fork or disable fork in the ancestor still sees them.
inline void obelisk_rt_reparent_process_children_unlocked(
    obelisk_rt_context *context, uint64_t parent, uint64_t replacement) {
  if (!context->logicalProcessParentsWithChildren.count(parent))
    return;
  bool reparented = false;
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.instance && process.parent == parent) {
      process.parent = replacement;
      reparented = true;
    }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (!task.terminated && task.parent == parent) {
      task.parent = replacement;
      reparented = true;
    }
  context->logicalProcessParentsWithChildren.erase(parent);
  if (reparented && replacement != 0)
    context->logicalProcessParentsWithChildren.insert(replacement);
}

obelisk_rt_status obelisk_rt_initialize_design_bytecode_image(
    const obelisk_rt_execution_descriptor_v1 &execution,
    obelisk::designbytecode::Image &image) noexcept;

DpiScopeHandle *obelisk_rt_find_dpi_scope(obelisk_rt_context *context,
                                          uint64_t id);
obelisk_rt_status obelisk_rt_initialize_dpi_scopes(
    obelisk_rt_context *context,
    const obelisk_rt_execution_descriptor_v1 *execution);

// Cold VPI query support. The caller holds the recursive context lock. This
// snapshots only canonical future scheduler calendars, never their heaps,
// mirrors, or deoptimization scratch state.
obelisk_rt_status obelisk_rt_snapshot_future_time_queues_unlocked(
    const obelisk_rt_context *context, std::vector<uint64_t> &times);
bool obelisk_rt_current_time_queue_pending_unlocked(
    obelisk_rt_context *context);
bool obelisk_rt_design_task_pending_before_read_only_unlocked(
    obelisk_rt_context *context);

template <typename Callable>
obelisk_rt_status guarded(obelisk_rt_context *context,
                          Callable &&callable) noexcept {
  OBELISK_RT_TRY { return callable(); }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setLastError(context, "runtime allocation failed");
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    setLastError(context, "unexpected runtime exception");
    return OBELISK_RT_IO_ERROR;
  }
}

// Feature-only counterpart to guarded().  Keeping the wrapper itself in the
// feature section prevents a cold callable from leaving a template
// instantiation in the ordinary runtime text.
template <typename Callable>
OBELISK_RT_FEATURE_HELPER obelisk_rt_status obelisk_rt_feature_guarded(
    obelisk_rt_context *context, Callable &&callable) noexcept {
  OBELISK_RT_TRY { return callable(); }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setLastError(context, "runtime allocation failed");
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    setLastError(context, "unexpected runtime exception");
    return OBELISK_RT_IO_ERROR;
  }
}

// The IEEE 1800-2017 21.2.1.7 rendering of one singular integral value inside
// an assignment pattern: the default decimal format of $display (21.2.1),
// unpadded. Shared so a container element reads the same as a struct member.
std::string obelisk_rt_pattern_integer_text(uint64_t width, bool isSigned,
                                            const uint64_t *value,
                                            const uint64_t *unknown);

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output);
bool validBytes(const void *data, uint64_t size);
std::string hostErrorMessage(int error);

// Cancel every not-yet-matured deferred-immediate report owned by one
// logical process. The caller must hold the context mutex.
void obelisk_rt_flush_deferred_immediate_reports_unlocked(
    obelisk_rt_context *context, uint64_t logicalProcess);
bool obelisk_rt_cancel_deferred_immediate_assertion_unlocked(
    obelisk_rt_context *context, uint64_t assertion,
    uint64_t logicalProcess = 0);

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
obelisk_rt_status obelisk_rt_execute_design_export(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept;
obelisk_rt_status obelisk_rt_execute_design_export_task(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount, const uint8_t *directions,
    const int64_t *const *aggregatePlans,
    const uint64_t *aggregatePlanWords) noexcept;
obelisk_rt_status
obelisk_rt_run_dpi_export_task_logical(obelisk_rt_context *context,
                                       uint64_t logical) noexcept;
obelisk_rt_status
obelisk_rt_initialize_design_state(obelisk_rt_context *context) noexcept;

// Generated process spawns are already bound to a validated context. Reuse
// its immutable design-bytecode image while retaining the public standalone
// creation entry point for descriptors without a context.
extern "C" obelisk_rt_status obelisk_rt_v1_process_instance_create_for_context(
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
obelisk_rt_status obelisk_rt_prime_design_task(obelisk_rt_context *context,
                                               uint64_t taskID) noexcept;
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
bool obelisk_rt_read_signal_bit_unlocked(obelisk_rt_context *context,
                                         uint64_t stableID, uint64_t bit,
                                         bool &value, bool &unknown,
                                         bool useSnapshot = true);
// Publish one packed transition mask for a committed signal range. The three
// planes retain per-bit edge identity so a batched NBA cannot spuriously wake
// a posedge waiter because another bit in the range had a posedge.
bool obelisk_rt_publish_signal_transition_batch_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset = 0, uint64_t *outSequence = nullptr,
    const uint8_t *oldValue = nullptr, const uint8_t *oldUnknown = nullptr,
    const uint8_t *newValue = nullptr, const uint8_t *newUnknown = nullptr);
bool obelisk_rt_publish_native_signal_transition_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown, bool indexedExternalDeposit = false,
    bool establishesOverride = false);
bool obelisk_rt_read_clock_condition_publication_bit_unlocked(
    const obelisk_rt_context *context, uint64_t stableID, uint64_t bit,
    bool &value, bool &unknown);
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
    obelisk_rt_context *context, obelisk_rt_computed_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    std::unique_ptr<SignalWaitLatch> &latch);
void obelisk_rt_unregister_signal_wait_unlocked(
    obelisk_rt_context *context,
    std::vector<std::unique_ptr<SignalSubscription>> &subscriptions,
    uint64_t waiterToken = 0, bool designWaiter = false);
bool obelisk_rt_notify_managed_waiters_unlocked(obelisk_rt_context *context,
                                                uint64_t token);
bool obelisk_rt_notify_observer_event_unlocked(obelisk_rt_context *context,
                                               uint64_t stableID);
bool obelisk_rt_initialize_event_order_wait_unlocked(
    obelisk_rt_context *context, const obelisk_rt_wait_record_v1 *wait,
    uint32_t &index, bool &ready, bool &failed);
bool obelisk_rt_notify_event_order_waiters_unlocked(obelisk_rt_context *context,
                                                    uint64_t stableID);
bool obelisk_rt_notify_observer_signal_unlocked(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint64_t width);
bool obelisk_rt_notify_observer_managed_unlocked(obelisk_rt_context *context,
                                                 uint64_t token);
bool obelisk_rt_evaluate_covergroup_managed_clock_events_unlocked(
    obelisk_rt_context *context, uint64_t token);
void obelisk_rt_notify_managed_watch(obelisk_rt_object_v1 *object,
                                     obelisk_rt_managed_watch_kind kind,
                                     uint64_t selector, uint64_t size = 0);
bool obelisk_rt_evaluate_design_observers_unlocked(obelisk_rt_context *context,
                                                   uint32_t dependencyKind,
                                                   uint64_t publishedHandle,
                                                   uint64_t publishedWidth);
bool obelisk_rt_evaluate_design_clock_condition_unlocked(
    obelisk_rt_context *context, uint64_t taskID, uint64_t codeUnitID,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t &value, uint64_t &unknown);
bool obelisk_rt_evaluate_design_bound_observer_unlocked(
    obelisk_rt_context *context, uint64_t taskID, uint64_t codeUnitID,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCapacity);
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
uint64_t
obelisk_rt_canonical_net_bit_unlocked(const obelisk_rt_context *context,
                                      uint64_t bit) noexcept;
void obelisk_rt_claim_override_range_unlocked(
    obelisk_rt_context *context, uint64_t begin, uint64_t width, bool assign,
    uint64_t owner, std::vector<uint64_t> &retiredOwners);
void obelisk_rt_release_override_range_unlocked(
    obelisk_rt_context *context, uint64_t begin, uint64_t width, bool assign,
    std::vector<uint64_t> &retiredOwners);
bool obelisk_rt_override_owner_matches_unlocked(
    const obelisk_rt_context *context, uint64_t bit, bool assign,
    uint64_t owner);
obelisk_rt_status
obelisk_rt_retire_override_owners(obelisk_rt_context *context,
                                  std::vector<uint64_t> retiredOwners);

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
obelisk_rt_status obelisk_rt_cached_design_parent(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
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
obelisk_rt_status obelisk_rt_cached_design_semantic_root(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 object,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept;
obelisk_rt_status obelisk_rt_cached_design_semantic_type_info(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_semantic_type_info_v1 *outInfo) noexcept;
obelisk_rt_status obelisk_rt_cached_design_semantic_type_edge(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_semantic_type_edge_v1 *outEdge) noexcept;
obelisk_rt_status obelisk_rt_cached_design_source(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outFile, uint64_t *outFileSize, uint32_t *outLine,
    uint32_t *outColumn) noexcept;

obelisk_rt_status obelisk_rt_cached_design_name(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outData, uint64_t *outSize) noexcept;

// Read a narrow bit window from one reflected storage object. This is the
// cold VPI/debugger path and deliberately preserves direct native state
// access without allocating buffers proportional to the containing object.
obelisk_rt_status
obelisk_rt_read_design_slice(obelisk_rt_context *context,
                             obelisk_rt_design_cursor_v1 cursor,
                             uint64_t bitOffset, uint64_t bitWidth,
                             uint64_t *value, uint64_t *unknown) noexcept;

// Resolve one reflected storage/net bit to its canonical global-state
// coordinate. Query-only consumers use this instead of confusing a source
// object ID with its independently allocated state offset.
obelisk_rt_status obelisk_rt_design_state_offset(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t bitOffset, uint64_t *outStateOffset) noexcept;

obelisk_rt_status obelisk_rt_cached_vpi_type(const obelisk_rt_context *context,
                                             obelisk_rt_design_cursor_v1 cursor,
                                             uint32_t *outType) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_fixed_property(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint32_t selector, VPIFixedPropertyValue *outValue) noexcept;
obelisk_rt_status
obelisk_rt_cached_vpi_frozen_value(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 cursor,
                                   VPIFrozenValue *outValue) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_resolved_net_type(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t bitOffset, uint64_t bitWidth, uint32_t *outType) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_net_delay(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t bitOffset, uint64_t bitWidth, VPINetDelayValue *outDelay) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_relation_range(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 source,
    uint32_t selector, bool iterate, VPIRelationRange *outRange) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_relation_target(
    const obelisk_rt_context *context, uint64_t relationIndex,
    obelisk_rt_design_cursor_v1 *outCursor, uint32_t *outType,
    bool *outStatement) noexcept;
obelisk_rt_status
obelisk_rt_cached_vpi_relation_index(const obelisk_rt_context *context,
                                     obelisk_rt_design_cursor_v1 source,
                                     VPIRelationIndexInfo *outInfo) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_relation_index_dimension(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    uint32_t dimension, int64_t *outLeft, int64_t *outRight) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_relation_index_key(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    int64_t index, uint32_t *outOrdinal) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_relation_index_ordinal_key(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    uint32_t ordinal, int64_t *outIndex) noexcept;
obelisk_rt_status
obelisk_rt_cached_vpi_array_member(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 member,
                                   VPIArrayMemberInfo *outInfo) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_statement_scope(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outScope) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_statement_enclosing_scope(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outScope, bool *outStatement) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_statement_parent(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outParent) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_statement_owner(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outOwner) noexcept;
obelisk_rt_status
obelisk_rt_cached_vpi_statement_is_scope(const obelisk_rt_context *context,
                                         obelisk_rt_design_cursor_v1 statement,
                                         bool *outIsScope) noexcept;
obelisk_rt_status obelisk_rt_cached_vpi_statement_is_protected(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    bool *outIsProtected) noexcept;

#endif // OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
