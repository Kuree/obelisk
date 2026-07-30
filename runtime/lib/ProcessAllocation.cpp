//===- ProcessAllocation.cpp - Process frame allocation pool ------------===//

#include "ProcessAllocation.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <optional>

namespace obelisk::process {
namespace {

struct ProcessFreeBlock {
  ProcessFreeBlock *next;
};
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

} // namespace

void *allocateProcessMemory(size_t size, size_t alignment) noexcept {
  if (ProcessAllocationPool *pool = processAllocationPool())
    return pool->allocate(size, alignment);
  return std::aligned_alloc(alignment, size);
}

void releaseProcessMemory(void *allocation, size_t size,
                          size_t alignment) noexcept {
  if (ProcessAllocationPool *pool = processAllocationPool())
    pool->release(allocation, size, alignment);
  else
    std::free(allocation);
}

} // namespace obelisk::process
