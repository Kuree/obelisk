//===- ReadySet.h - Ordered, allocation-free scheduler ready sets -*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_READYSET_H
#define OBELISK_RUNTIME_READYSET_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define OBELISK_READYSET_NOINLINE __attribute__((noinline))
#define OBELISK_READYSET_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define OBELISK_READYSET_NOINLINE __declspec(noinline)
#define OBELISK_READYSET_INLINE __forceinline
#else
#define OBELISK_READYSET_NOINLINE
#define OBELISK_READYSET_INLINE inline
#endif

namespace obelisk::runtime {

/// Physical word layout shared by runtime storage and generated LLVM code.
/// Leaves are authoritative. The cached word is a lower bound on the first
/// nonempty leaf; summaries contain exactly one bit per nonempty child word.
/// There is no allocation or capacity change during activation or dispatch.
template <bool CacheMinimum> struct BasicReadySetLayout {
  static constexpr uint32_t noBit = UINT32_MAX;
  // A short cached scan wins for moderate sets; summaries bound sparse scans
  // for larger universes. This threshold changes cost, never set semantics.
  static constexpr uint32_t flatWordLimit = 32;
  static constexpr unsigned maxLevels = 6;

  uint32_t capacity = 0;
  unsigned levels = 1;
  std::array<uint32_t, maxLevels> offsets{};
  std::array<uint32_t, maxLevels> counts{};
  uint32_t storageWords = 0;

  explicit BasicReadySetLayout(uint32_t bits = 0) : capacity(bits) {
    counts[0] = (uint64_t{bits} + 63) / 64;
    storageWords = counts[0];
    if (hasCache())
      ++storageWords;
    if (counts[0] > flatWordLimit)
      while (counts[levels - 1] > 1) {
        assert(levels < maxLevels);
        offsets[levels] = storageWords;
        counts[levels] = (uint64_t{counts[levels - 1]} + 63) / 64;
        storageWords += counts[levels];
        ++levels;
      }
  }

  bool hasCache() const { return CacheMinimum && counts[0] > 1; }
  bool hasSummaries() const { return levels > 1; }
  uint32_t cacheOffset() const { return counts[0]; }
  uint64_t lastWordMask() const {
    return (capacity & 63) ? (uint64_t{1} << (capacity & 63)) - 1 : UINT64_MAX;
  }
};

using ReadySetLayout = BasicReadySetLayout<true>;
using CursorReadySetLayout = BasicReadySetLayout<false>;

/// Mutable view over caller-owned storage in ReadySetLayout format. Neither
/// the storage nor the layout may move while this view is in use. Mutation is
/// serialized by the scheduler; parallel producers need per-worker sets and
/// a merge at the scheduler boundary, not unsynchronized writes to this view.
template <bool CacheMinimum> class BasicReadySetView {
public:
  using Layout = BasicReadySetLayout<CacheMinimum>;
  BasicReadySetView(uint64_t *storage, const Layout &layout)
      : storage(storage), layout(layout) {}
  BasicReadySetView(uint64_t *, Layout &&) = delete;

  uint64_t word(uint32_t index) const {
    assert(index < layout.counts[0]);
    return storage[index];
  }
  bool test(uint32_t bit) const {
    return bit < layout.capacity &&
           (storage[bit / 64] & (uint64_t{1} << (bit & 63))) != 0;
  }
  OBELISK_READYSET_INLINE bool set(uint32_t bit) {
    if (bit >= layout.capacity)
      return false;
    uint32_t index = bit / 64;
    uint64_t old = storage[index];
    uint64_t next = old | (uint64_t{1} << (bit & 63));
    storage[index] = next;
    if ((layout.hasCache() || layout.hasSummaries()) && old == 0)
      activateWord(index);
    return old != next;
  }
  OBELISK_READYSET_INLINE bool reset(uint32_t bit) {
    if (bit >= layout.capacity)
      return false;
    uint32_t index = bit / 64;
    uint64_t old = storage[index];
    uint64_t next = old & ~(uint64_t{1} << (bit & 63));
    storage[index] = next;
    if (layout.hasSummaries() && old && !next)
      updateSummaries(index, false);
    return old != next;
  }
  bool setWord(uint32_t index, uint64_t mask) {
    if (index >= layout.counts[0])
      return false;
    if (index + 1 == layout.counts[0])
      mask &= layout.lastWordMask();
    uint64_t old = storage[index];
    uint64_t next = old | mask;
    if (old == next)
      return false;
    storage[index] = next;
    if ((layout.hasCache() || layout.hasSummaries()) && old == 0)
      activateWord(index);
    return true;
  }
  bool clearWord(uint32_t index, uint64_t mask) {
    if (index >= layout.counts[0])
      return false;
    uint64_t old = storage[index];
    uint64_t next = old & ~mask;
    if (old == next)
      return false;
    storage[index] = next;
    if (next == 0)
      updateSummaries(index, false);
    // An empty cached word is a valid lower bound. Repair it lazily during
    // selection, before calling the executor, so reactivation cannot be lost.
    return true;
  }
  uint32_t findFirst() {
    if constexpr (!CacheMinimum)
      return findAtOrAfter(0);
    if (layout.counts[0] == 0)
      return ReadySetLayout::noBit;
    if (!layout.hasCache())
      return storage[0] ? trailingZeros(storage[0]) : ReadySetLayout::noBit;
    uint32_t first = storage[layout.cacheOffset()];
    if (first >= layout.counts[0])
      return ReadySetLayout::noBit;
    if (!storage[first]) {
      if (layout.hasSummaries()) {
        first = findInLevel(1, first);
        if (first == ReadySetLayout::noBit)
          first = layout.counts[0];
      } else {
        do {
          ++first;
        } while (first < layout.counts[0] && !storage[first]);
      }
      storage[layout.cacheOffset()] = first;
    }
    return first < layout.counts[0] ? first * 64 + trailingZeros(storage[first])
                                    : ReadySetLayout::noBit;
  }
  OBELISK_READYSET_INLINE uint32_t findAtOrAfter(uint32_t bit) const {
    // This must not advance the minimum cache: a cursor-based pass can leave
    // lower-order owners pending for its next iteration.
    if (bit >= layout.capacity)
      return ReadySetLayout::noBit;
    uint32_t index = bit / 64;
    uint64_t remaining = storage[index] & (UINT64_MAX << (bit & 63));
    if (remaining)
      return index * 64 + trailingZeros(remaining);
    // Keep the common leaf hit and short flat scan nonrecursive/inlinable.
    // Only sparse hierarchical misses need the recursive summary search.
    if (layout.hasSummaries()) {
      index = findInLevel(1, index + 1);
      if (index == ReadySetLayout::noBit)
        return index;
    } else {
      do {
        ++index;
      } while (index < layout.counts[0] && !storage[index]);
      if (index == layout.counts[0])
        return ReadySetLayout::noBit;
    }
    return index * 64 + trailingZeros(storage[index]);
  }
  uint32_t popFirst() {
    uint32_t bit = findFirst();
    if (bit != ReadySetLayout::noBit)
      reset(bit);
    return bit;
  }
  void clear() {
    if (layout.storageWords)
      std::fill(storage, storage + layout.storageWords, 0);
    if (layout.hasCache())
      storage[layout.cacheOffset()] = layout.counts[0];
  }
  /// Reestablish derived state after importing authoritative leaf words at a
  /// quiescent boundary. Direct leaf writes are otherwise forbidden.
  void rebuild() {
    if (layout.counts[0] == 0)
      return;
    storage[layout.counts[0] - 1] &= layout.lastWordMask();
    if (!layout.hasCache() && !layout.hasSummaries())
      return;
    std::fill(storage + layout.counts[0], storage + layout.storageWords, 0);
    if (layout.hasCache())
      storage[layout.cacheOffset()] = layout.counts[0];
    for (uint32_t index = 0; index < layout.counts[0]; ++index) {
      if (!storage[index])
        continue;
      activateWord(index);
    }
  }

private:
  void activateWord(uint32_t index) {
    if (layout.hasCache())
      storage[layout.cacheOffset()] =
          std::min(storage[layout.cacheOffset()], uint64_t{index});
    updateSummaries(index, true);
  }
  static unsigned trailingZeros(uint64_t word) {
    assert(word != 0);
    return static_cast<unsigned>(__builtin_ctzll(word));
  }
  void updateSummaries(uint32_t child, bool nonempty) {
    // Keep the hierarchy loop out of flat-set callers. Otherwise LTO outlines
    // the entire activation helper and makes every leaf update cross an ABI
    // boundary. Pass storage/layout explicitly so no temporary view escapes.
    if (layout.hasSummaries())
      updateSummaryWords(storage, layout, child, nonempty);
  }
  static OBELISK_READYSET_NOINLINE void updateSummaryWords(uint64_t *storage,
                                                           const Layout &layout,
                                                           uint32_t child,
                                                           bool nonempty) {
    for (unsigned level = 1; level < layout.levels; ++level) {
      uint32_t index = child / 64;
      uint64_t mask = uint64_t{1} << (child & 63);
      uint64_t &word = storage[layout.offsets[level] + index];
      uint64_t old = word;
      word = nonempty ? word | mask : word & ~mask;
      if ((old != 0) == (word != 0))
        break;
      child = index;
    }
  }
  uint32_t findInLevel(unsigned level, uint32_t bit) const {
    uint32_t index = bit / 64;
    if (index >= layout.counts[level])
      return ReadySetLayout::noBit;
    const uint64_t *words = storage + layout.offsets[level];
    uint64_t remaining = words[index] & (UINT64_MAX << (bit & 63));
    if (remaining)
      return index * 64 + trailingZeros(remaining);
    if (level + 1 < layout.levels) {
      index = findInLevel(level + 1, index + 1);
      if (index == ReadySetLayout::noBit)
        return index;
    } else {
      do {
        ++index;
      } while (index < layout.counts[level] && !words[index]);
      if (index == layout.counts[level])
        return ReadySetLayout::noBit;
    }
    assert(words[index] != 0);
    return index * 64 + trailingZeros(words[index]);
  }

  uint64_t *storage;
  const Layout &layout;
};

using ReadySetView = BasicReadySetView<true>;
using CursorReadySetView = BasicReadySetView<false>;

/// Runtime owner. Small sets use an inline word; generated code uses the same
/// layout with statically allocated words and does not instantiate this class.
template <bool CacheMinimum> class BasicReadySet {
public:
  using Layout = BasicReadySetLayout<CacheMinimum>;
  using View = BasicReadySetView<CacheMinimum>;
  explicit BasicReadySet(uint32_t capacity = 0) { resize(capacity); }
  BasicReadySet(const BasicReadySet &other)
      : layout(other.layout), inlineWord(other.inlineWord),
        storage(other.storage) {
    resetStoragePointer();
  }
  // Leave moved-from sets empty and usable without a mandatory resize.
  BasicReadySet(BasicReadySet &&other) noexcept { swap(other); }
  BasicReadySet &operator=(BasicReadySet other) noexcept {
    swap(other);
    return *this;
  }
  void swap(BasicReadySet &other) noexcept {
    std::swap(layout, other.layout);
    std::swap(inlineWord, other.inlineWord);
    storage.swap(other.storage);
    resetStoragePointer();
    other.resetStoragePointer();
  }
  /// Discard membership and prepare capacity; never called on the hot path.
  void resize(uint32_t capacity) {
    Layout next(capacity);
    // Allocate before changing the layout so allocation failure leaves the
    // existing set valid. uint64_t initialization cannot throw after
    // allocation.
    if (next.counts[0] > 1)
      storage.assign(next.storageWords, 0);
    else
      storage.clear();
    layout = next;
    resetStoragePointer();
    view().clear();
  }
  uint32_t capacity() const { return layout.capacity; }
  uint32_t wordCount() const { return layout.counts[0]; }
  const Layout &getLayout() const { return layout; }
  uint64_t word(uint32_t index) const { return view().word(index); }
  bool test(uint32_t bit) const { return view().test(bit); }
  OBELISK_READYSET_INLINE bool set(uint32_t bit) { return view().set(bit); }
  OBELISK_READYSET_INLINE bool reset(uint32_t bit) { return view().reset(bit); }
  bool setWord(uint32_t index, uint64_t mask) {
    return view().setWord(index, mask);
  }
  bool clearWord(uint32_t index, uint64_t mask) {
    return view().clearWord(index, mask);
  }
  uint32_t findFirst() { return view().findFirst(); }
  OBELISK_READYSET_INLINE uint32_t findAtOrAfter(uint32_t bit) const {
    return view().findAtOrAfter(bit);
  }
  uint32_t popFirst() { return view().popFirst(); }
  void clear() { view().clear(); }

private:
  void resetStoragePointer() {
    words = layout.counts[0] > 1 ? storage.data() : &inlineWord;
  }
  View view() { return {words, layout}; }
  // Read-only operations on the view do not mutate its storage.
  View view() const { return {words, layout}; }
  Layout layout;
  uint64_t inlineWord = 0;
  std::vector<uint64_t> storage;
  // Like an inline-capacity vector, carry a direct leaf pointer so dispatch
  // does not select inline versus heap storage on every membership operation.
  uint64_t *words = &inlineWord;
};

using ReadySet = BasicReadySet<true>;
// Forward graph passes carry their own cursor and ask for the global minimum
// only on wraparound. Do not maintain an unused minimum cache on every wakeup;
// retain the same leaf representation and hierarchical sparse-scan bound.
using CursorReadySet = BasicReadySet<false>;

} // namespace obelisk::runtime

#undef OBELISK_READYSET_NOINLINE
#undef OBELISK_READYSET_INLINE

#endif // OBELISK_RUNTIME_READYSET_H
