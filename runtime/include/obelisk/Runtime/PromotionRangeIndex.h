//===- PromotionRangeIndex.h - Canonical range-to-proof dependencies -----===//

#ifndef OBELISK_RUNTIME_PROMOTIONRANGEINDEX_H
#define OBELISK_RUNTIME_PROMOTIONRANGEINDEX_H

#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

namespace obelisk::runtime {

// Compiler-side construction. Merge only ranges of the same certificate;
// physical overlap does not make two independently guarded proofs aliases.
inline bool buildPromotionRangeIndex(
    std::vector<obelisk_rt_native_promotion_dependency> &entries,
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
  uint64_t prefixEnd = 0;
  for (auto &entry : entries)
    entry.prefix_end = prefixEnd = std::max(prefixEnd, entry.end);
  return true;
}

// The immutable table has been verified by the compiler. Callback effects
// must be idempotent: a wide write can intersect separate ranges of one proof.
// No allocation, design-state access, or scheduler entry is performed here.
template <typename Invalidate>
inline void visitPromotionRangeDependencies(
    const obelisk_rt_native_promotion_dependency *entries, uint64_t count,
    uint64_t offset, uint64_t width, Invalidate &&invalidate) {
  if (!width || !count)
    return;
  uint64_t end = width > UINT64_MAX - offset ? UINT64_MAX : offset + width;
  uint64_t low = 0, high = count;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (entries[middle].begin < end)
      low = middle + 1;
    else
      high = middle;
  }
  while (low && entries[low - 1].prefix_end > offset) {
    const auto &entry = entries[--low];
    if (entry.end > offset)
      invalidate(entry.certificate);
  }
}

} // namespace obelisk::runtime

#endif
