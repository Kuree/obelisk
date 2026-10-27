//===- PromotionRangeIndex.h - Canonical range-to-proof dependencies -----===//

#ifndef TALOS_RUNTIME_PROMOTIONRANGEINDEX_H
#define TALOS_RUNTIME_PROMOTIONRANGEINDEX_H

#include "talos/Runtime/Runtime.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

namespace talos::runtime {

// Compiler-side construction. Merge only ranges of the same certificate;
// physical overlap does not make two independently guarded proofs aliases.
inline bool buildPromotionRangeIndex(
    std::vector<talos_rt_native_promotion_dependency> &entries,
    uint64_t stateBits, uint64_t certificateCount) {
  for (const auto &entry : entries)
    if (entry.begin >= entry.end || entry.end > stateBits ||
        entry.certificate >= certificateCount)
      return false;
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    return std::tie(a.certificate, a.begin, a.end) <
           std::tie(b.certificate, b.begin, b.end);
  });
  size_t size = 0;
  for (auto entry : entries) {
    if (size && entries[size - 1].certificate == entry.certificate &&
        entry.begin <= entries[size - 1].end) {
      entries[size - 1].end = std::max(entries[size - 1].end, entry.end);
    } else {
      entries[size++] = entry;
    }
  }
  entries.resize(size);
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    return std::tie(a.begin, a.end, a.certificate) <
           std::tie(b.begin, b.end, b.certificate);
  });
  // LRM 6.11.2, 38.34: unknown-plane changes must reach every dependent
  // proof, including overlapping spans. A balanced interval index lets a
  // long memory dependency coexist with small ranges without a linear scan.
  auto index = [&](auto &&self, uint64_t low, uint64_t high) -> uint64_t {
    if (low == high)
      return 0;
    uint64_t middle = low + (high - low) / 2;
    auto &entry = entries[middle];
    entry.subtree_end =
        std::max(entry.end, std::max(self(self, low, middle),
                                     self(self, middle + 1, high)));
    return entry.subtree_end;
  };
  index(index, 0, entries.size());
  return true;
}

// The immutable table has been verified by the compiler. Callback effects
// must be idempotent: a wide write can intersect separate ranges of one proof.
// No allocation, design-state access, or scheduler entry is performed here.
template <typename Invalidate>
inline void visitPromotionRangeDependencies(
    const talos_rt_native_promotion_dependency *entries, uint64_t count,
    uint64_t offset, uint64_t width, Invalidate &&invalidate) {
  if (!width || !count)
    return;
  uint64_t end = width > UINT64_MAX - offset ? UINT64_MAX : offset + width;
  auto visit = [&](auto &&self, uint64_t low, uint64_t high) -> void {
    while (low != high && entries[low].begin < end) {
      uint64_t middle = low + (high - low) / 2;
      const auto &entry = entries[middle];
      if (entry.subtree_end <= offset)
        return;
      // IEEE 1800-2023 6.3.1, 38.34: X/Z changes must reach every affected
      // proof, including overlapping ranges. Sorted starts exclude this
      // entry and its right subtree together, so descend left without a call.
      if (entry.begin >= end) {
        high = middle;
        continue;
      }
      // Keep in-order visits, skipping disjoint left trees before recursion
      // and visiting the right subtree iteratively.
      if (low != middle &&
          entries[low + (middle - low) / 2].subtree_end > offset)
        self(self, low, middle);
      if (entry.end > offset)
        invalidate(entry.certificate);
      low = middle + 1;
    }
  };
  visit(visit, 0, count);
}

// Optional compiler-built index for narrow queries. Each 64-bit bucket owns
// an ascending slice of packed (certificate:48, begin:8, end:8) records. Bounds
// are relative to the bucket; end may equal 64. The byte budget includes both
// arrays and prevents sparse or very wide dependencies from expanding the IR.
struct PromotionBucketIndex {
  std::vector<uint32_t> starts;
  std::vector<uint64_t> records;
};

inline bool buildPromotionBucketIndex(
    const std::vector<talos_rt_native_promotion_dependency> &entries,
    uint64_t maxBytes, PromotionBucketIndex &result) {
  result = {};
  if (entries.empty() || maxBytes < sizeof(uint32_t))
    return false;
  uint64_t end = 0, count = 0;
  for (const auto &entry : entries) {
    if (entry.begin >= entry.end || entry.certificate >> 48)
      return false;
    end = std::max(end, entry.end);
    uint64_t added = (entry.end - 1) / 64 - entry.begin / 64 + 1;
    if (added > UINT32_MAX - count)
      return false;
    count += added;
  }
  // The rounded extent must remain representable by the query helper.
  if (end > UINT64_MAX - 63)
    return false;
  uint64_t buckets = (end + 63) / 64;
  if (buckets > maxBytes / sizeof(uint32_t) - 1 ||
      buckets >= std::numeric_limits<size_t>::max())
    return false;
  uint64_t prefixBytes = (buckets + 1) * sizeof(uint32_t);
  if (count > (maxBytes - prefixBytes) / sizeof(uint64_t) ||
      count > std::numeric_limits<size_t>::max())
    return false;
  result.starts.resize(buckets + 1);
  for (const auto &entry : entries)
    for (uint64_t bucket = entry.begin / 64; bucket <= (entry.end - 1) / 64;
         ++bucket)
      ++result.starts[bucket + 1];
  for (uint64_t bucket = 1; bucket <= buckets; ++bucket)
    result.starts[bucket] += result.starts[bucket - 1];
  result.records.resize(count);
  auto next = result.starts;
  for (const auto &entry : entries)
    for (uint64_t bucket = entry.begin / 64; bucket <= (entry.end - 1) / 64;
         ++bucket) {
      uint64_t base = bucket * 64;
      uint64_t low = entry.begin > base ? entry.begin - base : 0;
      uint64_t high = std::min(entry.end - base, uint64_t{64});
      result.records[next[bucket]++] =
          (entry.certificate << 16) | (low << 8) | high;
    }
  return true;
}

// IEEE 1800-2023 6.3.1, 6.11.2, 38.34: preserve every intersecting proof for
// X/Z changes, including overlapping aliases and cross-bucket writes. Repeated
// certificate visits are idempotent, just as in the interval index. No state
// access, allocation, actor, design callback or scheduler entry occurs here.
template <typename Invalidate>
inline void visitPromotionBucketDependencies(const uint32_t *starts,
                                             const uint64_t *records,
                                             uint64_t bucketCount,
                                             uint64_t offset, uint64_t width,
                                             Invalidate &&invalidate) {
  if (!width || offset >= bucketCount * 64)
    return;
  uint64_t end = width > UINT64_MAX - offset ? UINT64_MAX : offset + width;
  end = std::min(end, bucketCount * 64);
  for (uint64_t bucket = offset / 64; bucket <= (end - 1) / 64; ++bucket) {
    uint64_t base = bucket * 64;
    uint64_t low = offset > base ? offset - base : 0;
    uint64_t high = std::min(end - base, uint64_t{64});
    for (uint32_t index = starts[bucket]; index != starts[bucket + 1];
         ++index) {
      uint64_t record = records[index];
      if (((record >> 8) & 255) < high && (record & 255) > low)
        invalidate(record >> 16);
    }
  }
}

} // namespace talos::runtime

#endif
