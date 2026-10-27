//===- DescriptorOrdering.h - Fixed-width descriptor ordering ----*- C++
//-*-===//

#ifndef TALOS_ANALYSIS_DESCRIPTORORDERING_H
#define TALOS_ANALYSIS_DESCRIPTORORDERING_H

#include "llvm/ADT/SmallVector.h"
#include <array>
#include <cstdint>

namespace talos::analysis {

// Descriptor IDs, ordinals, and slots have fixed-width unsigned keys. Eight
// radix passes give deterministic linear ordering, including sparse keys.
template <typename T, typename Key>
void orderByUnsignedKey(llvm::SmallVectorImpl<T> &records, Key key) {
  if (records.size() < 2)
    return;
  llvm::SmallVector<T> scratch(records.size());
  for (unsigned shift = 0; shift != 64; shift += 8) {
    std::array<size_t, 256> positions{};
    for (const T &record : records)
      ++positions[(uint64_t(key(record)) >> shift) & 255];
    size_t begin = 0;
    for (size_t &position : positions) {
      size_t count = position;
      position = begin;
      begin += count;
    }
    for (const T &record : records)
      scratch[positions[(uint64_t(key(record)) >> shift) & 255]++] = record;
    records.swap(scratch);
  }
}

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_DESCRIPTORORDERING_H
