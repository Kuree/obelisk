//===- RandomDomain.h - Finite semantic random-domain wire records --------===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include <cstddef>
#include <cstdint>

namespace obelisk::random {

inline uint32_t domainRead32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

inline uint64_t domainRead64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t(p[byte]) << (byte * 8);
  return value;
}

/// Mask/value cubes use inline words in V1 and narrow V2 domains. Wide V2
/// cubes reference disjoint, little-endian word ranges in the literal pool.
/// Keeping the record size fixed preserves the offsets of following metadata.
struct DomainRecord {
  uint32_t group = 0;
  uint32_t targetOffset = 0;
  uint32_t width = 0;
  const uint8_t *mask = nullptr;
  const uint8_t *value = nullptr;

  uint64_t words() const { return (uint64_t(width) + 63) / 64; }
  uint64_t maskWord(uint64_t word) const {
    return domainRead64(mask + word * 8);
  }
  uint64_t valueWord(uint64_t word) const {
    return domainRead64(value + word * 8);
  }
};

/// The caller first validates the enclosing record and literal-pool extents.
/// When supplied, literalUse rejects aliased or reused pool words; the caller
/// also checks that every pool word is used exactly once after all decoding.
inline bool readDomainRecord(const uint8_t *p, uint32_t aggregateWidth,
                             bool version2, const uint8_t *literalPool,
                             uint32_t literalWords, uint8_t *literalUse,
                             DomainRecord &result) {
  result.group = domainRead32(p);
  result.targetOffset = domainRead32(p + 4);
  result.width = domainRead32(p + 8);
  uint32_t flags = domainRead32(p + 12);
  if (!result.width || result.targetOffset > aggregateWidth ||
      result.width > aggregateWidth - result.targetOffset)
    return false;
  if (flags == 0) {
    if (result.width > 64)
      return false;
    result.mask = p + 16;
    result.value = p + 24;
    if (result.width == 64 && !result.maskWord(0))
      return false;
  } else if (flags == OBELISK_RT_RANDOM_DOMAIN_LITERAL_WORDS) {
    if (!version2 || result.width <= 64)
      return false;
    uint64_t indices[] = {domainRead64(p + 16), domainRead64(p + 24)};
    for (uint64_t index : indices) {
      if (index > literalWords || result.words() > literalWords - index)
        return false;
      if (literalUse)
        for (uint64_t word = 0; word != result.words(); ++word) {
          if (literalUse[index + word])
            return false;
          literalUse[index + word] = 1;
        }
    }
    result.mask = literalPool + size_t(indices[0]) * 8;
    result.value = literalPool + size_t(indices[1]) * 8;
  } else {
    return false;
  }
  for (uint64_t word = 0; word != result.words(); ++word)
    if (result.valueWord(word) & ~result.maskWord(word))
      return false;
  unsigned highBits = result.width % 64;
  return !highBits || ((result.maskWord(result.words() - 1) |
                        result.valueWord(result.words() - 1)) >>
                       highBits) == 0;
}

inline bool domainPatternsOverlap(const DomainRecord &a,
                                  const DomainRecord &b) {
  for (uint64_t word = 0; word != a.words(); ++word)
    if ((a.valueWord(word) ^ b.valueWord(word)) &
        (a.maskWord(word) & b.maskWord(word)))
      return false;
  return true;
}

} // namespace obelisk::random
