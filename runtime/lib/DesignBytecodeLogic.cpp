//===- DesignBytecodeLogic.cpp - Bytecode integer value semantics -------===//

#include "DesignBytecodeLogic.h"
#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace obelisk::designbytecode {

bool anyUnknown(const Logic &value) {
  return std::any_of(value.unknown.begin(), value.unknown.end(),
                     [](uint64_t limb) { return limb != 0; });
}
bool isZero(const Logic &value) {
  return std::all_of(value.value.begin(), value.value.end(),
                     [](uint64_t limb) { return limb == 0; });
}
Logic allX(uint32_t width, bool fourState) {
  Logic result{width, fourState, std::vector<uint64_t>(limbCount(width), 0),
               std::vector<uint64_t>(limbCount(width), UINT64_MAX)};
  result.unknown.back() &= finalMask(width);
  return result;
}
void mask(Logic &value) {
  value.value.back() &= finalMask(value.width);
  value.unknown.back() &= finalMask(value.width);
}

int compareUnsigned(const std::vector<uint64_t> &left,
                    const std::vector<uint64_t> &right) {
  // Callers always pass equal-width operands (enforced upstream by the
  // SameOperandsAndResultType / SameTypeOperands op traits). Guard the
  // invariant so a future IR regression trips here instead of reading past
  // the shorter vector.
  assert(left.size() == right.size() && "compareUnsigned width mismatch");
  for (size_t index = left.size(); index != 0; --index)
    if (left[index - 1] != right[index - 1])
      return left[index - 1] < right[index - 1] ? -1 : 1;
  return 0;
}

Logic negate(Logic value) {
  for (uint64_t &limb : value.value)
    limb = ~limb;
  uint64_t carry = 1;
  for (uint64_t &limb : value.value) {
    uint64_t old = limb;
    limb += carry;
    carry = carry && limb < old;
  }
  mask(value);
  return value;
}

double integerToDouble(Logic integer, bool isSigned) {
  bool negative =
      isSigned &&
      ((integer.value[(integer.width - 1) / 64] >> ((integer.width - 1) % 64)) &
       1) != 0;
  if (negative)
    integer = negate(std::move(integer));

  size_t high = integer.value.size();
  while (high != 0 && integer.value[high - 1] == 0)
    --high;
  if (high == 0)
    return 0.0;

  uint64_t highLimb = integer.value[high - 1];
  unsigned highBits = 0;
  for (uint64_t scan = highLimb; scan != 0; scan >>= 1)
    ++highBits;
  uint64_t activeBits = (high - 1) * 64 + highBits;
  if (activeBits <= 64) {
    double result = static_cast<double>(integer.value.front());
    return negative ? -result : result;
  }

  uint64_t exponent = activeBits - 1;
  if (exponent > 1023)
    return negative ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity();

  uint64_t shift = activeBits - 53;
  size_t word = static_cast<size_t>(shift / 64);
  unsigned bit = static_cast<unsigned>(shift % 64);
  uint64_t mantissa = integer.value[word] >> bit;
  if (bit != 0 && word + 1 < integer.value.size())
    mantissa |= integer.value[word + 1] << (64 - bit);
  mantissa &= UINT64_C(0x1fffffffffffff);

  uint64_t halfwayPosition = shift - 1;
  size_t halfwayWord = static_cast<size_t>(halfwayPosition / 64);
  unsigned halfwayBit = static_cast<unsigned>(halfwayPosition % 64);
  bool halfway = ((integer.value[halfwayWord] >> halfwayBit) & 1) != 0;
  bool lower = false;
  for (size_t index = 0; index < halfwayWord; ++index)
    lower |= integer.value[index] != 0;
  if (halfwayBit != 0)
    lower |=
        (integer.value[halfwayWord] & ((uint64_t{1} << halfwayBit) - 1)) != 0;
  if (halfway && (lower || (mantissa & 1) != 0))
    ++mantissa;
  if (mantissa == (uint64_t{1} << 53)) {
    mantissa >>= 1;
    if (++exponent > 1023)
      return negative ? -std::numeric_limits<double>::infinity()
                      : std::numeric_limits<double>::infinity();
  }

  uint64_t encoded = (negative ? uint64_t{1} << 63 : 0) |
                     ((exponent + 1023) << 52) |
                     (mantissa & UINT64_C(0x000fffffffffffff));
  double result;
  std::memcpy(&result, &encoded, sizeof(result));
  return result;
}

float integerToFloat(Logic integer, bool isSigned) {
  bool negative =
      isSigned &&
      ((integer.value[(integer.width - 1) / 64] >> ((integer.width - 1) % 64)) &
       1) != 0;
  if (negative)
    integer = negate(std::move(integer));

  size_t high = integer.value.size();
  while (high != 0 && integer.value[high - 1] == 0)
    --high;
  if (high == 0)
    return 0.0f;

  uint64_t highLimb = integer.value[high - 1];
  unsigned highBits = 0;
  for (uint64_t scan = highLimb; scan != 0; scan >>= 1)
    ++highBits;
  uint64_t activeBits = (high - 1) * 64 + highBits;
  if (activeBits <= 64) {
    float result = static_cast<float>(integer.value.front());
    return negative ? -result : result;
  }

  uint64_t exponent = activeBits - 1;
  if (exponent > 127)
    return negative ? -std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::infinity();

  uint64_t shift = activeBits - 24;
  size_t word = static_cast<size_t>(shift / 64);
  unsigned bit = static_cast<unsigned>(shift % 64);
  uint64_t mantissa = integer.value[word] >> bit;
  if (bit != 0 && word + 1 < integer.value.size())
    mantissa |= integer.value[word + 1] << (64 - bit);
  mantissa &= UINT64_C(0xffffff);

  uint64_t halfwayPosition = shift - 1;
  size_t halfwayWord = static_cast<size_t>(halfwayPosition / 64);
  unsigned halfwayBit = static_cast<unsigned>(halfwayPosition % 64);
  bool halfway = ((integer.value[halfwayWord] >> halfwayBit) & 1) != 0;
  bool lower = false;
  for (size_t index = 0; index < halfwayWord; ++index)
    lower |= integer.value[index] != 0;
  if (halfwayBit != 0)
    lower |=
        (integer.value[halfwayWord] & ((uint64_t{1} << halfwayBit) - 1)) != 0;
  if (halfway && (lower || (mantissa & 1) != 0))
    ++mantissa;
  if (mantissa == (uint64_t{1} << 24)) {
    mantissa >>= 1;
    if (++exponent > 127)
      return negative ? -std::numeric_limits<float>::infinity()
                      : std::numeric_limits<float>::infinity();
  }

  uint32_t encoded = (negative ? uint32_t{1} << 31 : 0) |
                     (static_cast<uint32_t>(exponent + 127) << 23) |
                     (static_cast<uint32_t>(mantissa) & UINT32_C(0x007fffff));
  float result;
  std::memcpy(&result, &encoded, sizeof(result));
  return result;
}

Logic doubleToInteger(double value, uint32_t width) {
  uint64_t encoded;
  std::memcpy(&encoded, &value, sizeof(encoded));
  bool negative = (encoded >> 63) != 0;
  uint64_t exponentBits = (encoded >> 52) & UINT64_C(0x7ff);
  int64_t exponent = static_cast<int64_t>(exponentBits) - 1023;
  uint64_t mantissa =
      (encoded & UINT64_C(0x000fffffffffffff)) | (uint64_t{1} << 52);

  Logic integer{width, false, std::vector<uint64_t>(limbCount(width)),
                std::vector<uint64_t>(limbCount(width))};
  if (exponent == -1) {
    integer.value.front() = 1;
  } else if (exponent >= 0 && exponentBits != UINT64_C(0x7ff)) {
    if (exponent < 52) {
      unsigned shift = static_cast<unsigned>(52 - exponent);
      integer.value.front() =
          (mantissa >> shift) + ((mantissa >> (shift - 1)) & 1);
    } else {
      uint64_t shift = static_cast<uint64_t>(exponent - 52);
      size_t word = static_cast<size_t>(shift / 64);
      unsigned bit = static_cast<unsigned>(shift % 64);
      if (word < integer.value.size())
        integer.value[word] |= mantissa << bit;
      if (bit != 0 && word + 1 < integer.value.size())
        integer.value[word + 1] |= mantissa >> (64 - bit);
    }
  }
  mask(integer);
  return negative ? negate(std::move(integer)) : integer;
}

Logic add(const Logic &left, const Logic &right, bool subtract) {
  assert(left.value.size() == right.value.size() && "add width mismatch");
  if (anyUnknown(left) || anyUnknown(right))
    return allX(left.width, left.fourState);
  Logic rhs = subtract ? negate(right) : right;
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  uint64_t carry = 0;
  for (size_t index = 0; index != result.value.size(); ++index) {
    uint64_t first = left.value[index] + rhs.value[index];
    uint64_t carry0 = first < left.value[index];
    uint64_t second = first + carry;
    uint64_t carry1 = second < first;
    result.value[index] = second;
    carry = carry0 | carry1;
  }
  mask(result);
  return result;
}

struct WideProduct {
  uint64_t low;
  uint64_t high;
};

constexpr WideProduct multiply64Portable(uint64_t left, uint64_t right) {
  uint64_t leftLow = static_cast<uint32_t>(left);
  uint64_t leftHigh = left >> 32;
  uint64_t rightLow = static_cast<uint32_t>(right);
  uint64_t rightHigh = right >> 32;
  uint64_t lowLow = leftLow * rightLow;
  uint64_t lowHigh = leftLow * rightHigh;
  uint64_t highLow = leftHigh * rightLow;
  uint64_t highHigh = leftHigh * rightHigh;
  uint64_t middle = (lowLow >> 32) + static_cast<uint32_t>(lowHigh) +
                    static_cast<uint32_t>(highLow);
  return {(middle << 32) | static_cast<uint32_t>(lowLow),
          highHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32)};
}

static_assert(multiply64Portable(UINT64_MAX, UINT64_MAX).low == 1);
static_assert(multiply64Portable(UINT64_MAX, UINT64_MAX).high ==
              UINT64_MAX - 1);
static_assert(multiply64Portable(UINT64_C(1) << 63, UINT64_C(1) << 63).high ==
              (UINT64_C(1) << 62));

Logic multiply(const Logic &left, const Logic &right) {
  assert(left.value.size() == right.value.size() && "multiply width mismatch");
  if (anyUnknown(left) || anyUnknown(right))
    return allX(left.width, left.fourState);
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  for (size_t i = 0; i != left.value.size(); ++i) {
    uint64_t carry = 0;
    for (size_t j = 0; j + i < result.value.size(); ++j) {
      WideProduct product = multiply64Portable(left.value[i], right.value[j]);
      uint64_t sum = product.low + result.value[i + j];
      uint64_t carry0 = sum < product.low;
      uint64_t withCarry = sum + carry;
      uint64_t carry1 = withCarry < sum;
      result.value[i + j] = withCarry;
      carry = product.high + carry0 + carry1;
    }
  }
  mask(result);
  return result;
}

bool bit(const std::vector<uint64_t> &value, uint64_t index) {
  return ((value[index / 64] >> (index % 64)) & 1) != 0;
}
void setBit(std::vector<uint64_t> &value, uint64_t index, bool enabled) {
  uint64_t mask = uint64_t{1} << (index % 64);
  value[index / 64] =
      enabled ? value[index / 64] | mask : value[index / 64] & ~mask;
}

std::pair<Logic, Logic> divide(const Logic &dividend, const Logic &divisor,
                               bool isSigned) {
  assert(dividend.value.size() == divisor.value.size() &&
         "divide width mismatch");
  if (anyUnknown(dividend) || anyUnknown(divisor) || isZero(divisor))
    return {allX(dividend.width, dividend.fourState),
            allX(dividend.width, dividend.fourState)};
  bool dividendNegative = isSigned && bit(dividend.value, dividend.width - 1);
  bool divisorNegative = isSigned && bit(divisor.value, divisor.width - 1);
  Logic numerator = dividendNegative ? negate(dividend) : dividend;
  Logic denominator = divisorNegative ? negate(divisor) : divisor;
  Logic quotient{dividend.width, dividend.fourState,
                 std::vector<uint64_t>(dividend.value.size()),
                 std::vector<uint64_t>(dividend.value.size())};
  Logic remainder = quotient;
  for (uint64_t index = dividend.width; index != 0; --index) {
    uint64_t carry = bit(numerator.value, index - 1);
    for (uint64_t &limb : remainder.value) {
      uint64_t next = limb >> 63;
      limb = (limb << 1) | carry;
      carry = next;
    }
    mask(remainder);
    if (compareUnsigned(remainder.value, denominator.value) >= 0) {
      remainder = add(remainder, denominator, true);
      setBit(quotient.value, index - 1, true);
    }
  }
  if (dividendNegative != divisorNegative)
    quotient = negate(quotient);
  if (dividendNegative)
    remainder = negate(remainder);
  return {quotient, remainder};
}

Logic bitwise(const Logic &left, const Logic &right, uint16_t opcode) {
  assert(left.value.size() == right.value.size() && "bitwise width mismatch");
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  for (size_t index = 0; index != result.value.size(); ++index) {
    uint64_t lv = left.value[index], rv = right.value[index];
    uint64_t lu = left.unknown[index], ru = right.unknown[index];
    if (!left.fourState) {
      result.value[index] = opcode == OBELISK_RT_DB_AND  ? lv & rv
                            : opcode == OBELISK_RT_DB_OR ? lv | rv
                                                         : lv ^ rv;
      continue;
    }
    uint64_t lk = ~lu, rk = ~ru;
    if (opcode == OBELISK_RT_DB_AND) {
      uint64_t knownZero = (~lv & lk) | (~rv & rk);
      uint64_t knownOne = (lv & lk) & (rv & rk);
      result.value[index] = knownOne;
      result.unknown[index] = ~(knownZero | knownOne);
    } else if (opcode == OBELISK_RT_DB_OR) {
      uint64_t knownOne = (lv & lk) | (rv & rk);
      uint64_t knownZero = (~lv & lk) & (~rv & rk);
      result.value[index] = knownOne;
      result.unknown[index] = ~(knownZero | knownOne);
    } else {
      result.unknown[index] = lu | ru;
      result.value[index] = (lv ^ rv) & ~result.unknown[index];
    }
  }
  mask(result);
  return result;
}

Logic shift(const Logic &input, const Logic &amount, uint16_t opcode) {
  if (anyUnknown(amount))
    return allX(input.width, input.fourState);
  bool oversized = false;
  uint64_t distance = amount.value.empty() ? 0 : amount.value[0];
  for (size_t index = 1; index < amount.value.size(); ++index)
    oversized |= amount.value[index] != 0;
  oversized |= distance >= input.width;
  Logic result{input.width, input.fourState,
               std::vector<uint64_t>(input.value.size()),
               std::vector<uint64_t>(input.value.size())};
  bool arithmetic = opcode == OBELISK_RT_DB_ASHR;
  bool signValue = arithmetic && bit(input.value, input.width - 1);
  bool signUnknown = arithmetic && bit(input.unknown, input.width - 1);
  for (uint64_t destination = 0; destination != input.width; ++destination) {
    bool fill = false;
    uint64_t source = 0;
    if (opcode == OBELISK_RT_DB_SHL) {
      fill = !oversized && destination >= distance;
      source = destination - std::min<uint64_t>(destination, distance);
    } else {
      fill = !oversized && destination + distance < input.width;
      source = destination + distance;
    }
    setBit(result.value, destination,
           fill ? bit(input.value, source) : arithmetic && signValue);
    setBit(result.unknown, destination,
           fill ? bit(input.unknown, source) : arithmetic && signUnknown);
  }
  return result;
}

} // namespace obelisk::designbytecode
