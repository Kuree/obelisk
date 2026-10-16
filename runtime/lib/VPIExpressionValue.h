//===- VPIExpressionValue.h - Integral expression values ----------------===//

#ifndef OBELISK_RUNTIME_LIB_VPIEXPRESSIONVALUE_H
#define OBELISK_RUNTIME_LIB_VPIEXPRESSIONVALUE_H

#include "DesignBytecodeLogic.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/Runtime.h"
#include <array>

namespace obelisk::vpiexpression {
using designbytecode::LimbVector;
using designbytecode::Logic;
using reflection::VPIIntegralExpressionShape;

inline Logic resized(const Logic &input, uint32_t width, bool sign,
                     bool fourState) {
  using namespace designbytecode;
  Logic result{width, fourState, LimbVector(limbCount(width)),
               LimbVector(limbCount(width))};
  for (uint32_t bitIndex = 0; bitIndex != width; ++bitIndex) {
    if (bitIndex >= input.width && !sign)
      break;
    uint32_t source = bitIndex < input.width ? bitIndex : input.width - 1;
    bool unknown = bit(input.unknown, source);
    setBit(result.value, bitIndex,
           bit(input.value, source) && (fourState || !unknown));
    if (fourState)
      setBit(result.unknown, bitIndex, unknown);
  }
  return result;
}

// False=0, true=1, ambiguous=2. A known one dominates unknown bits when
// converting an integral operand to a logical condition (11.4.7).
inline uint32_t truth(const Logic &input) {
  bool unknown = false;
  for (size_t index = 0; index != input.value.size(); ++index) {
    if (input.value[index] & ~input.unknown[index])
      return 1;
    unknown |= input.unknown[index] != 0;
  }
  return unknown ? 2 : 0;
}

inline Logic boolean(uint32_t truthValue, bool fourState) {
  return Logic{1,
               fourState,
               {uint64_t(truthValue == 1)},
               {uint64_t(truthValue == 2 && fourState)}};
}

// The immutable image has already proved the arity, result shape and each
// context-sensitive child's effective shape. These pure operations never
// execute a source call or modify a declaration.
inline Logic evaluate(uint32_t code, VPIIntegralExpressionShape result,
                      const std::array<Logic, 3> &values,
                      const std::array<VPIIntegralExpressionShape, 3> &shapes) {
  using namespace designbytecode;
  using Rule = reflection::VPIIntegralOperatorRule;
  Rule rule = reflection::integralOperatorRule(code);
  uint32_t width = static_cast<uint32_t>(result.width);
  if (rule == Rule::Cast)
    return resized(values[0], width, shapes[0].isSigned, result.fourState);
  auto operand = [&](size_t index) {
    return resized(values[index], width, result.isSigned, result.fourState);
  };
  auto inverted = [](Logic value) {
    for (size_t index = 0; index != value.value.size(); ++index)
      value.value[index] = ~value.value[index] & ~value.unknown[index];
    mask(value);
    return value;
  };
  if (rule == Rule::Reduction) {
    if (code == 3) {
      auto condition = truth(values[0]);
      return boolean(condition == 2 ? 2 : !condition, result.fourState);
    }
    bool zero = false, one = false, unknown = false, parity = false;
    for (uint32_t index = 0; index != values[0].width; ++index) {
      bool u = bit(values[0].unknown, index), v = bit(values[0].value, index);
      zero |= !u && !v;
      one |= !u && v;
      unknown |= u;
      parity ^= !u && v;
    }
    uint32_t answer = code <= 6   ? (zero      ? 0
                                     : unknown ? 2
                                               : 1)
                      : code <= 8 ? (one       ? 1
                                     : unknown ? 2
                                               : 0)
                                  : (unknown ? 2 : uint32_t(parity));
    if ((code == 6 || code == 8 || code == 10) && answer != 2)
      answer = !answer;
    return boolean(answer, result.fourState);
  }
  if (rule == Rule::Logical) {
    uint32_t left = truth(values[0]), right = truth(values[1]);
    if (code == 50 && left != 2)
      left = !left;
    uint32_t answer = code == 26 ? (!left || !right           ? 0
                                    : left == 2 || right == 2 ? 2
                                                              : 1)
                                 : (left == 1 || right == 1   ? 1
                                    : left == 2 || right == 2 ? 2
                                                              : 0);
    return boolean(answer, result.fourState);
  }
  if (rule == Rule::Comparison) {
    uint32_t commonWidth = static_cast<uint32_t>(
        shapes[0].width > shapes[1].width ? shapes[0].width : shapes[1].width);
    bool sign = shapes[0].isSigned && shapes[1].isSigned;
    bool state = shapes[0].fourState || shapes[1].fourState;
    Logic left = resized(values[0], commonWidth, sign, state);
    Logic right = resized(values[1], commonWidth, sign, state);
    if (code == 16 || code == 17) {
      bool equal = left.value == right.value && left.unknown == right.unknown;
      return boolean(code == 16 ? equal : !equal, result.fourState);
    }
    if (code == 14 || code == 15 || code == 69 || code == 70) {
      bool wildcard = code >= 69, mismatch = false, ambiguous = false;
      for (uint32_t index = 0; index != commonWidth; ++index) {
        bool lu = bit(left.unknown, index), ru = bit(right.unknown, index);
        if (wildcard && ru)
          continue;
        if (lu || ru)
          ambiguous = true;
        else
          mismatch |= bit(left.value, index) != bit(right.value, index);
      }
      bool unequal = code == 15 || code == 70;
      return boolean(mismatch    ? uint32_t(unequal)
                     : ambiguous ? 2
                                 : uint32_t(!unequal),
                     result.fourState);
    }
    if (anyUnknown(left) || anyUnknown(right))
      return boolean(2, result.fourState);
    int compared = compareUnsigned(left.value, right.value);
    if (sign &&
        bit(left.value, commonWidth - 1) != bit(right.value, commonWidth - 1))
      compared = bit(left.value, commonWidth - 1) ? -1 : 1;
    bool answer = code == 18   ? compared > 0
                  : code == 19 ? compared >= 0
                  : code == 20 ? compared < 0
                               : compared <= 0;
    return boolean(answer, result.fourState);
  }
  if (rule == Rule::Conditional) {
    uint32_t condition = truth(values[0]);
    if (condition != 2)
      return operand(condition ? 1 : 2);
    Logic left = operand(1), right = operand(2);
    // Table 11-20: only equal known bits survive an ambiguous condition.
    for (size_t index = 0; index != left.value.size(); ++index) {
      uint64_t unknown = left.unknown[index] | right.unknown[index] |
                         (left.value[index] ^ right.value[index]);
      left.value[index] &= ~unknown;
      left.unknown[index] = result.fourState ? unknown : 0;
    }
    mask(left);
    return left;
  }
  Logic left = operand(0);
  if (rule == Rule::Unary) {
    if (code == 2)
      return left;
    if (code == 4)
      return inverted(std::move(left));
    return anyUnknown(left) ? allX(width, result.fourState)
                            : negate(std::move(left));
  }
  if (rule == Rule::Left) {
    if (code != 43) {
      uint16_t opcode = code == 22 || code == 41        ? OBELISK_RT_DB_SHL
                        : code == 42 && result.isSigned ? OBELISK_RT_DB_ASHR
                                                        : OBELISK_RT_DB_LSHR;
      return shift(left, values[1], opcode);
    }
    const Logic &exponent = values[1];
    if (anyUnknown(left) || anyUnknown(exponent))
      return allX(width, result.fourState);
    if (shapes[1].isSigned && bit(exponent.value, exponent.width - 1)) {
      if (isZero(left))
        return allX(width, result.fourState);
      Logic one{width, result.fourState, LimbVector(limbCount(width)),
                LimbVector(limbCount(width))};
      one.value[0] = 1;
      if (left.value == one.value)
        return one;
      Logic minusOne = negate(one);
      if (result.isSigned && left.value == minusOne.value)
        return bit(exponent.value, 0) ? minusOne : one;
      one.value[0] = 0;
      return one;
    }
    return power(left, exponent);
  }
  Logic right = operand(1);
  switch (code) {
  case 11:
    return add(left, right, true);
  case 24:
    return add(left, right, false);
  case 25:
    return multiply(left, right);
  case 12:
    return divide(left, right, result.isSigned).first;
  case 13:
    return divide(left, right, result.isSigned).second;
  case 28:
    return bitwise(left, right, OBELISK_RT_DB_AND);
  case 29:
    return bitwise(left, right, OBELISK_RT_DB_OR);
  case 30:
    return bitwise(left, right, OBELISK_RT_DB_XOR);
  case 31:
    return inverted(bitwise(left, right, OBELISK_RT_DB_XOR));
  default:
    return allX(width, result.fourState);
  }
}
} // namespace obelisk::vpiexpression

#endif // OBELISK_RUNTIME_LIB_VPIEXPRESSIONVALUE_H
