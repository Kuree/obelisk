//===- RandSolveWeighted.h - Exact distribution metadata ------------------===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace obelisk::runtime {

struct WeightedInterval {
  uint64_t lower;
  // Zero is the complete 64-bit domain, whose count is 2^64.
  uint64_t cardinality;
};

struct WeightedItem {
  uint64_t identity;
  uint32_t weightCapture;
  bool perRange;
  std::vector<WeightedInterval> intervals;
};

struct WeightedGroup {
  uint32_t targetOffset;
  unsigned width;
  uint32_t constraintBlock;
  // UINT32_MAX denotes a hard distribution.
  uint32_t softPriority;
  bool isSigned;
  std::vector<WeightedItem> items;
};

/// Parse the raw mass policy independently of host packing and endianness.
/// The caller supplies only this footer's bytes, excluding later sections.
inline bool parseWeightedGroups(const uint8_t *bytes, size_t size,
                                uint32_t aggregateWidth, uint32_t captureCount,
                                std::vector<WeightedGroup> &groups) {
  auto read16 = [](const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
  };
  auto read32 = [](const uint8_t *p) {
    uint32_t value = 0;
    for (unsigned byte = 0; byte != 4; ++byte)
      value |= uint32_t(p[byte]) << (byte * 8);
    return value;
  };
  auto read64 = [](const uint8_t *p) {
    uint64_t value = 0;
    for (unsigned byte = 0; byte != 8; ++byte)
      value |= uint64_t(p[byte]) << (byte * 8);
    return value;
  };
  if (size < OBELISK_RT_RANDOM_DIST_HEADER_SIZE)
    return false;
  uint32_t count = read32(bytes);
  uint32_t records = read32(bytes + 4);
  if (count == 0 || count > records ||
      records != (size - OBELISK_RT_RANDOM_DIST_HEADER_SIZE) /
                     OBELISK_RT_RANDOM_DIST_RECORD_SIZE ||
      (size - OBELISK_RT_RANDOM_DIST_HEADER_SIZE) %
              OBELISK_RT_RANDOM_DIST_RECORD_SIZE !=
          0)
    return false;
  groups.assign(count, {});
  std::vector<std::unordered_map<uint64_t, size_t>> items(count);
  const uint8_t *record = bytes + OBELISK_RT_RANDOM_DIST_HEADER_SIZE;
  for (uint32_t index = 0; index != records;
       ++index, record += OBELISK_RT_RANDOM_DIST_RECORD_SIZE) {
    uint32_t groupIndex = read32(record);
    uint32_t block = read32(record + 4);
    uint32_t offset = read32(record + 8);
    unsigned width = read16(record + 12);
    unsigned soft = read16(record + 14);
    uint64_t lower = read64(record + 16);
    uint64_t cardinality = read64(record + 24);
    uint64_t identity = read64(record + 32);
    uint32_t capture = read32(record + 40);
    uint32_t flags = read32(record + 44);
    if (groupIndex >= count || width == 0 || width > 64 ||
        offset > aggregateWidth || width > aggregateWidth - offset ||
        (block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 && block >= 64) ||
        capture >= captureCount || soft > 64 ||
        !(flags & OBELISK_RT_RANDOM_DIST_RAW_WEIGHT) ||
        (flags & ~(OBELISK_RT_RANDOM_DIST_RAW_WEIGHT |
                   OBELISK_RT_RANDOM_DIST_PER_RANGE |
                   OBELISK_RT_RANDOM_DIST_TARGET_SIGNED)))
      return false;
    uint64_t mask = width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
    if (lower > mask || (cardinality == 0 ? width != 64 || lower != 0
                                          : cardinality - 1 > mask - lower))
      return false;
    bool signedTarget = (flags & OBELISK_RT_RANDOM_DIST_TARGET_SIGNED) != 0;
    bool perRange = (flags & OBELISK_RT_RANDOM_DIST_PER_RANGE) != 0;
    WeightedGroup &group = groups[groupIndex];
    uint32_t softPriority = soft == 0 ? UINT32_MAX : soft - 1;
    if (group.items.empty()) {
      group.targetOffset = offset;
      group.width = width;
      group.constraintBlock = block;
      group.softPriority = softPriority;
      group.isSigned = signedTarget;
    } else if (group.targetOffset != offset || group.width != width ||
               group.constraintBlock != block ||
               group.softPriority != softPriority ||
               group.isSigned != signedTarget) {
      return false;
    }
    auto [found, inserted] =
        items[groupIndex].try_emplace(identity, group.items.size());
    if (inserted) {
      group.items.push_back({identity, capture, perRange, {}});
    } else if (!perRange ||
               group.items[found->second].weightCapture != capture ||
               !group.items[found->second].perRange) {
      return false;
    }
    group.items[found->second].intervals.push_back({lower, cardinality});
  }
  for (const WeightedGroup &group : groups)
    if (group.items.empty())
      return false;
  return true;
}

} // namespace obelisk::runtime
