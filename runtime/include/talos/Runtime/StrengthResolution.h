//===- StrengthResolution.h - Shared net strength algebra --------*- C++ -*-===//
//
// IEEE 1800-2017 28.12 strength combination, shared by the runtime resolver
// and the compiler, which evaluates the same rules to build bitwise
// resolution logic. A strength set is a mask over the signed scale -7..7:
// bit 7 is high impedance, bits below are 0 strengths and bits above are 1
// strengths.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_RUNTIME_STRENGTHRESOLUTION_H
#define TALOS_RUNTIME_STRENGTHRESOLUTION_H

#include <cstdint>
#include <cstdlib>

namespace talos::runtime {

inline constexpr unsigned strengthCount = 15;

inline uint16_t strengthBit(int strength) {
  return static_cast<uint16_t>(uint16_t{1} << (strength + 7));
}

/// The implicit contribution of a net of `resolution` kind (NetResolutionKind
/// numbering): tri0/tri1 pull (6.6.5) and supply0/supply1 supply (6.6.6)
/// drives; every other kind starts at high impedance.
inline uint16_t implicitNetStrength(uint8_t resolution) {
  switch (resolution) {
  case 5:
    return strengthBit(-5); // tri0
  case 6:
    return strengthBit(5); // tri1
  case 7:
    return strengthBit(-7); // supply0
  case 8:
    return strengthBit(7); // supply1
  default:
    return strengthBit(0); // high impedance
  }
}

/// Combine two strength sets on a net of `resolution` kind (NetResolutionKind
/// numbering: 3 is wand/triand, 4 is wor/trior).
inline uint16_t combineStrengthRanges(uint16_t lhs, uint16_t rhs,
                                      uint8_t resolution = 0) {
  uint16_t result = 0;
  for (unsigned lhsIndex = 0; lhsIndex != strengthCount; ++lhsIndex) {
    if ((lhs & (uint16_t{1} << lhsIndex)) == 0)
      continue;
    int left = static_cast<int>(lhsIndex) - 7;
    for (unsigned rhsIndex = 0; rhsIndex != strengthCount; ++rhsIndex) {
      if ((rhs & (uint16_t{1} << rhsIndex)) == 0)
        continue;
      int right = static_cast<int>(rhsIndex) - 7;
      if (left == 0 || right == 0) {
        result |= strengthBit(left == 0 ? right : left);
        continue;
      }
      if ((left < 0) == (right < 0)) {
        result |= strengthBit(std::abs(left) >= std::abs(right) ? left : right);
        continue;
      }
      if (std::abs(left) != std::abs(right)) {
        result |= strengthBit(std::abs(left) > std::abs(right) ? left : right);
        continue;
      }
      // IEEE 1800-2017 28.12.4 applies wired logic to equal-strength
      // conflicts. wand/triand select 0; wor/trior select 1.
      if (resolution == 3 || resolution == 4) {
        int magnitude = std::abs(left);
        result |= strengthBit(resolution == 3 ? -magnitude : magnitude);
        continue;
      }
      for (int strength = -std::abs(left); strength <= std::abs(left);
           ++strength)
        result |= strengthBit(strength);
    }
  }
  // Ambiguous strengths are ranges, not sparse sets (28.12.2). Preserve that
  // invariant so a later driver observes every intermediate strength level.
  unsigned first = 0;
  while (first != strengthCount && (result & (uint16_t{1} << first)) == 0)
    ++first;
  unsigned last = strengthCount;
  while (last != first && (result & (uint16_t{1} << (last - 1))) == 0)
    --last;
  uint16_t range = 0;
  for (unsigned index = first; index != last; ++index)
    range |= uint16_t{1} << index;
  return range;
}

/// The strength set one driver bit contributes. `strength0` and `strength1`
/// are 0 (highz) through 7 (supply); an unknown bit with value 1 is z.
inline uint16_t driverBitStrengths(bool value, bool unknown, unsigned strength0,
                                   unsigned strength1) {
  if (unknown && value)
    return strengthBit(0);
  if (!unknown)
    return value ? strengthBit(static_cast<int>(strength1))
                 : strengthBit(-static_cast<int>(strength0));
  uint16_t range = 0;
  for (int strength = -static_cast<int>(strength0);
       strength <= static_cast<int>(strength1); ++strength)
    range |= strengthBit(strength);
  return range;
}

/// The value and unknown planes of a resolved strength set: z when only high
/// impedance remains, a known 0 or 1 when every level lies on one side, and x
/// otherwise.
struct ResolvedBit {
  bool value;
  bool unknown;
};
inline ResolvedBit resolvedBit(uint16_t strengths) {
  bool negative = (strengths & 0x007f) != 0;
  bool highZ = (strengths & strengthBit(0)) != 0;
  bool positive = (strengths & 0x7f00) != 0;
  bool z = highZ && !negative && !positive;
  bool unknown = z || (negative + highZ + positive) != 1;
  return {z || (!unknown && positive), unknown};
}

} // namespace talos::runtime

#endif // TALOS_RUNTIME_STRENGTHRESOLUTION_H
