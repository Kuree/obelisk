//===- TimedUpdateQueue.h - bounded-width deferred update calendar -*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_LIB_TIMEDUPDATEQUEUE_H
#define OBELISK_RUNTIME_LIB_TIMEDUPDATEQUEUE_H

#include "obelisk/Runtime/Runtime.h"

#include <array>
#include <cassert>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// Scheduler time and enqueue sequence never move backwards. A radix heap
// orders their 128-bit pair without comparison sorting. Each update moves
// through at most 128 buckets over its lifetime, giving O(N) total queue work,
// including distinct delayed deadlines. NBA and Re-NBA have separate heaps.
// Merely inspecting a future deadline does not advance the heap's lower bound.
template <typename Update> class TimedUpdateQueue {
  using Key = std::pair<uint64_t, uint64_t>;
  static constexpr size_t BucketCount = 129;
  struct Heap {
    Key lowerBound{0, 0};
    std::array<std::vector<Update>, BucketCount> buckets;
    std::array<size_t, BucketCount> minima{};
    size_t count = 0;

    static Key key(const Update &update) {
      return {update.dueTime, update.sequence};
    }
    static size_t width(uint64_t bits) {
      size_t result = 0;
      while (bits) {
        ++result;
        bits >>= 1;
      }
      return result;
    }
    static size_t bucket(Key value, Key bound) {
      uint64_t time = value.first ^ bound.first;
      return time ? 64 + width(time) : width(value.second ^ bound.second);
    }
    size_t firstBucket() const {
      for (size_t index = 0; index != BucketCount; ++index)
        if (!buckets[index].empty())
          return index;
      return BucketCount;
    }
    const Update *front() const {
      if (count == 0)
        return nullptr;
      size_t index = firstBucket();
      return index == BucketCount ? nullptr : &buckets[index][minima[index]];
    }
    void insert(Update update) {
      Key value = key(update);
      assert(value >= lowerBound && "scheduler calendar moved backwards");
      size_t index = bucket(value, lowerBound);
      auto &entries = buckets[index];
      size_t ordinal = entries.size();
      entries.push_back(std::move(update));
      if (ordinal == 0 || value < key(entries[minima[index]]))
        minima[index] = ordinal;
      ++count;
    }
    Update pop() {
      size_t index = firstBucket();
      assert(index != BucketCount);
      if (index != 0) {
        Key next = key(buckets[index][minima[index]]);
        std::array<size_t, BucketCount> counts{};
        for (const Update &entry : buckets[index])
          ++counts[bucket(key(entry), next)];
        // Reserve before mutating any key or entry. Allocation failure leaves
        // every scheduled root and pending update in the original calendar.
        for (size_t target = 0; target != index; ++target)
          if (counts[target])
            buckets[target].reserve(buckets[target].size() + counts[target]);
        lowerBound = next;
        std::vector<Update> entries;
        entries.swap(buckets[index]);
        count -= entries.size();
        for (Update &entry : entries)
          insert(std::move(entry));
      }
      assert(buckets[0].size() == 1 && "enqueue sequences must be unique");
      Update result = std::move(buckets[0].back());
      buckets[0].pop_back();
      --count;
      if (count == 0)
        lowerBound = {0, 0};
      return result;
    }
  };
  std::array<std::unique_ptr<Heap>, 2> heaps;

  static size_t regionIndex(uint32_t region) {
    assert(region == OBELISK_RT_REGION_NBA ||
           region == OBELISK_RT_REGION_RE_NBA);
    return region == OBELISK_RT_REGION_NBA ? 0 : 1;
  }

public:
  template <bool Const> class Iterator {
    using Queue =
        std::conditional_t<Const, const TimedUpdateQueue, TimedUpdateQueue>;
    Queue *queue = nullptr;
    size_t region = 2, bucket = 0, ordinal = 0;
    friend class TimedUpdateQueue;
    void advance() {
      while (region != 2) {
        if (queue->heaps[region]) {
          while (bucket != BucketCount) {
            if (ordinal < queue->heaps[region]->buckets[bucket].size())
              return;
            ++bucket;
            ordinal = 0;
          }
        }
        ++region;
        bucket = ordinal = 0;
      }
    }
    Iterator(Queue *queue, size_t region) : queue(queue), region(region) {
      advance();
    }

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Update;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const Update *, Update *>;
    using reference = std::conditional_t<Const, const Update &, Update &>;
    Iterator() = default;
    reference operator*() const {
      return queue->heaps[region]->buckets[bucket][ordinal];
    }
    pointer operator->() const { return &**this; }
    Iterator &operator++() {
      ++ordinal;
      advance();
      return *this;
    }
    bool operator==(const Iterator &other) const {
      return region == other.region && bucket == other.bucket &&
             ordinal == other.ordinal;
    }
    bool operator!=(const Iterator &other) const { return !(*this == other); }
  };
  auto begin() { return Iterator<false>(this, 0); }
  auto end() { return Iterator<false>(this, 2); }
  auto begin() const { return Iterator<true>(this, 0); }
  auto end() const { return Iterator<true>(this, 2); }
  size_t size() const {
    return (heaps[0] ? heaps[0]->count : 0) + (heaps[1] ? heaps[1]->count : 0);
  }
  bool empty() const { return size() == 0; }
  void clear() {
    heaps[0].reset();
    heaps[1].reset();
  }
  void push_back(Update update) {
    size_t region = regionIndex(update.execRegion);
    if (!heaps[region])
      heaps[region] = std::make_unique<Heap>();
    heaps[region]->insert(std::move(update));
  }
  const Update *front(uint32_t region) const {
    if (region != OBELISK_RT_REGION_NBA && region != OBELISK_RT_REGION_RE_NBA)
      return nullptr;
    const auto &heap = heaps[regionIndex(region)];
    return heap ? heap->front() : nullptr;
  }
  Update pop(uint32_t region) { return heaps[regionIndex(region)]->pop(); }
};

#endif
