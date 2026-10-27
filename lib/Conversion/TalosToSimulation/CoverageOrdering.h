//===- CoverageOrdering.h - Linear coverage inventory ordering -----------===//

#ifndef TALOS_CONVERSION_TALOSTOSIMULATION_COVERAGEORDERING_H
#define TALOS_CONVERSION_TALOSTOSIMULATION_COVERAGEORDERING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallVectorExtras.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace talos::simlowering {

// Coverage identities, enum tags and ordinals are nonnegative, fixed-width
// fields. Keep composite keys as separate words to avoid truncating identities
// or allocating by a possibly sparse ordinal's maximum value.
template <typename... Fields> auto coverageOrderKey(Fields... fields) {
  return std::array<uint64_t, sizeof...(Fields)>{
      static_cast<uint64_t>(fields)...};
}

// Stable least-significant-byte radix ordering takes O(N) time and space for
// each fixed record key. Sort indices so records with large payloads move only
// during the final permutation. Skip constant bytes and ordered inventories.
template <size_t Fields>
llvm::SmallVector<size_t>
getCoverageOrder(llvm::ArrayRef<std::array<uint64_t, Fields>> keys) {
  static_assert(Fields != 0);
  llvm::SmallVector<size_t> order;
  order.reserve(keys.size());
  if (keys.empty())
    return order;
  bool sorted = true;
  auto previous = keys.front();
  std::array<uint64_t, Fields> differingBits{};
  for (auto [index, key] : llvm::enumerate(keys)) {
    order.push_back(index);
    sorted &= previous <= key;
    for (size_t field = 0; field != Fields; ++field)
      differingBits[field] |= key[field] ^ keys.front()[field];
    previous = key;
  }
  if (sorted)
    return order;

  llvm::SmallVector<size_t> scratch(keys.size());
  for (size_t field = Fields; field-- != 0;) {
    for (unsigned shift = 0; shift != 64; shift += 8) {
      if (!((differingBits[field] >> shift) & 0xff))
        continue;
      std::array<size_t, 256> offsets{};
      for (const auto &key : keys)
        ++offsets[(key[field] >> shift) & 0xff];
      size_t offset = 0;
      for (size_t &count : offsets) {
        size_t next = offset + count;
        count = offset;
        offset = next;
      }
      for (size_t index : order)
        scratch[offsets[(keys[index][field] >> shift) & 0xff]++] = index;
      order.swap(scratch);
    }
  }
  return order;
}

template <typename Records, typename Key>
void sortCoverageRecords(Records &records, Key key) {
  if (records.size() < 2)
    return;
  auto keys = llvm::map_to_vector(records, key);
  auto order = getCoverageOrder(llvm::ArrayRef(keys));
  // Apply each permutation cycle in place. Identity entries also mark visited
  // positions, avoiding a second record buffer or an additional visited set.
  for (size_t start = 0; start != order.size(); ++start) {
    if (order[start] == start)
      continue;
    auto saved = std::move(records[start]);
    size_t current = start;
    while (order[current] != start) {
      size_t next = order[current];
      records[current] = std::move(records[next]);
      order[current] = current;
      current = next;
    }
    records[current] = std::move(saved);
    order[current] = current;
  }
}

} // namespace talos::simlowering

#endif // TALOS_CONVERSION_TALOSTOSIMULATION_COVERAGEORDERING_H
