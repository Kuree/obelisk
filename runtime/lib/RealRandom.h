//===- RealRandom.h - Numeric coordinates for real random variables ------===//

#pragma once

#include "RandModelCount.h"

#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace obelisk::runtime {

/// The coordinate quantum is half the least positive subnormal. Converting
/// a coordinate towards zero gives each floating value a cell whose mass is
/// proportional to its numeric spacing, rather than one mass per encoding.
/// The half quantum also retains a coordinate for negative zero.
template <typename Float> struct RealFormat;
template <> struct RealFormat<double> {
  using Word = uint64_t;
  static constexpr unsigned fractionBits = 52, exponentBits = 11;
};
template <> struct RealFormat<float> {
  using Word = uint32_t;
  static constexpr unsigned fractionBits = 23, exponentBits = 8;
};

struct RealCoordinate {
  ModelCount magnitude;
  bool negative = false;
};

inline int compareCoordinates(const RealCoordinate &a,
                              const RealCoordinate &b) {
  if (a.negative != b.negative)
    return a.negative ? -1 : 1;
  int order = a.magnitude.compare(b.magnitude);
  return a.negative ? -order : order;
}

template <typename Float> class RealCoordinates {
  using Format = RealFormat<Float>;
  using Word = typename Format::Word;
  static_assert(std::numeric_limits<Float>::is_iec559);
  static_assert(sizeof(Float) == sizeof(Word));
  static constexpr unsigned fractionBits = Format::fractionBits;
  static constexpr unsigned exponentBits = Format::exponentBits;
  static constexpr uint64_t fractionMask = (uint64_t{1} << fractionBits) - 1;
  static constexpr uint64_t exponentMask = (uint64_t{1} << exponentBits) - 1;
  static constexpr uint64_t signMask = uint64_t{1}
                                       << (fractionBits + exponentBits);
  static constexpr uint64_t maximumWord =
      ((exponentMask - 1) << fractionBits) | fractionMask;

  static Float floating(uint64_t word) {
    Word storage = static_cast<Word>(word);
    Float value;
    std::memcpy(&value, &storage, sizeof(value));
    return value;
  }

  static ModelCount maximumMagnitude() {
    return ModelCount((uint64_t{1} << (fractionBits + 1)) - 1)
        .shifted(exponentMask - 1);
  }

  static RealCoordinate coordinate(Float value) {
    uint64_t bits = encoding(value);
    uint64_t fraction = bits & fractionMask;
    unsigned exponent = (bits >> fractionBits) & exponentMask;
    bool negative = (bits & signMask) != 0;
    ModelCount magnitude =
        exponent ? ModelCount((uint64_t{1} << fractionBits) | fraction)
                       .shifted(exponent)
                 : ModelCount(fraction).shifted(1);
    if (negative && magnitude.zero())
      magnitude = ModelCount(1);
    return {std::move(magnitude), negative};
  }

  static uint64_t shiftedWord(const ModelCount &magnitude, unsigned shift) {
    unsigned word = shift / 64, bits = shift % 64;
    uint64_t value = magnitude.word(word) >> bits;
    if (bits)
      value |= magnitude.word(word + 1) << (64 - bits);
    return value;
  }

  static Float floating(const RealCoordinate &value) {
    unsigned bits = value.magnitude.bitWidth();
    uint64_t word = value.negative ? signMask : 0;
    if (bits <= fractionBits + 1)
      word |= shiftedWord(value.magnitude, 1);
    else {
      unsigned exponent = bits - fractionBits - 1;
      word |= uint64_t(exponent) << fractionBits;
      word |= shiftedWord(value.magnitude, exponent) & fractionMask;
    }
    return floating(word);
  }

  static Float nextUp(Float value) {
    uint64_t bits = encoding(value);
    if ((bits & ~signMask) == 0)
      return floating(1);
    return floating((bits & signMask) ? bits - 1 : bits + 1);
  }

  static Float nextDown(Float value) {
    uint64_t bits = encoding(value);
    if ((bits & ~signMask) == 0)
      return floating(signMask | 1);
    return floating((bits & signMask) ? bits + 1 : bits - 1);
  }

  static RealCoordinate firstCoordinate(Float value) {
    if (value == Float(0))
      return {ModelCount(1), true};
    RealCoordinate first = coordinate(value);
    if (first.negative && encoding(value) != (signMask | maximumWord)) {
      first = coordinate(nextDown(value));
      first.magnitude.subtract(ModelCount(1));
    }
    return first;
  }

  static RealCoordinate lastCoordinate(Float value) {
    if (value == Float(0))
      return {ModelCount(1), false};
    RealCoordinate last = coordinate(value);
    if (!last.negative && encoding(value) != maximumWord) {
      last = coordinate(nextUp(value));
      last.magnitude.subtract(ModelCount(1));
    }
    return last;
  }

public:
  static constexpr unsigned rankBits =
      (uint64_t{1} << exponentBits) - 2 + fractionBits + 2;
  static constexpr unsigned rankWords = (rankBits + 63) / 64;
  static constexpr unsigned highBits = rankBits - (rankWords - 1) * 64;
  static constexpr uint64_t highMask = (uint64_t{1} << highBits) - 1;

  static uint64_t encoding(Float value) {
    Word word;
    std::memcpy(&word, &value, sizeof(word));
    return word;
  }

  static bool finite(Float value) {
    return ((encoding(value) >> fractionBits) & exponentMask) != exponentMask;
  }

  static bool encode(Float value, uint64_t *out) {
    if (!finite(value))
      return false;
    RealCoordinate input = coordinate(value);
    std::array<uint64_t, rankWords> words{};
    uint64_t carry = input.negative ? 1 : 0;
    for (unsigned i = 0; i != rankWords; ++i) {
      uint64_t word = input.magnitude.word(i);
      if (input.negative) {
        word = ~word + carry;
        carry = carry && word == 0;
      }
      words[i] = word;
    }
    words.back() &= highMask;
    std::copy(words.begin(), words.end(), out);
    return true;
  }

  static bool decode(const uint64_t *input, Float &out) {
    if (input[rankWords - 1] & ~highMask)
      return false;
    bool negative = (input[rankWords - 1] >> (highBits - 1)) != 0;
    std::array<uint64_t, rankWords> words{};
    uint64_t carry = negative ? 1 : 0;
    for (unsigned i = 0; i != rankWords; ++i) {
      uint64_t word = input[i];
      if (negative) {
        word = ~word + carry;
        carry = carry && word == 0;
      }
      words[i] = word;
    }
    words.back() &= highMask;
    ModelCount magnitude = ModelCount::fromWords(words.data(), rankWords);
    if (magnitude.compare(maximumMagnitude()) > 0)
      return false;
    out = floating({std::move(magnitude), negative});
    return true;
  }

  static bool sample(obelisk_rt_random_state_v1 &state, Float lower,
                     Float upper, uint32_t flags, Float &out) {
    Float first = flags & OBELISK_RT_REAL_LOWER_OPEN ? nextUp(lower) : lower;
    Float last = flags & OBELISK_RT_REAL_UPPER_OPEN ? nextDown(upper) : upper;
    if (!finite(first) || !finite(last) || first > last)
      return false;
    if (first == last) {
      out = first;
      return true;
    }
    RealCoordinate start = firstCoordinate(first);
    RealCoordinate end = lastCoordinate(last);
    if (compareCoordinates(start, end) > 0)
      return false;
    ModelCount cardinality;
    if (start.negative != end.negative) {
      cardinality = start.magnitude;
      cardinality.add(end.magnitude);
    } else if (start.negative) {
      cardinality = start.magnitude;
      cardinality.subtract(end.magnitude);
    } else {
      cardinality = end.magnitude;
      cardinality.subtract(start.magnitude);
    }
    cardinality.add(ModelCount(1));
    ModelCount rank = ModelCount::randomBelow(cardinality, state);
    RealCoordinate selected;
    if (start.negative && rank.compare(start.magnitude) < 0) {
      selected.magnitude = start.magnitude;
      selected.magnitude.subtract(rank);
      selected.negative = true;
    } else if (start.negative) {
      rank.subtract(start.magnitude);
      selected.magnitude = std::move(rank);
    } else {
      rank.add(start.magnitude);
      selected.magnitude = std::move(rank);
    }
    out = floating(selected);
    return true;
  }
};

static_assert(RealCoordinates<double>::rankBits == OBELISK_RT_REAL64_RANK_BITS);
static_assert(RealCoordinates<double>::rankWords ==
              OBELISK_RT_REAL64_RANK_WORDS);
static_assert(RealCoordinates<float>::rankBits == OBELISK_RT_REAL32_RANK_BITS);
static_assert(RealCoordinates<float>::rankWords ==
              OBELISK_RT_REAL32_RANK_WORDS);

} // namespace obelisk::runtime
