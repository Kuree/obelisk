//===- SimulationVPIConstantValues.h - Pure VPI constant arithmetic -------===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONVPICONSTANTVALUES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONVPICONSTANTVALUES_H

#include "obelisk/Reflection/VPIObjectModel.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include <algorithm>
#include <optional>
#include <utility>

namespace obelisk::sim {

struct VPIConstantValue {
  llvm::APInt value;
  llvm::APInt unknown;
};

inline VPIConstantValue resizeVPIConstant(const VPIConstantValue &input,
                                          unsigned width, bool sign,
                                          bool fourState) {
  auto value =
      sign ? input.value.sextOrTrunc(width) : input.value.zextOrTrunc(width);
  auto unknown = sign ? input.unknown.sextOrTrunc(width)
                      : input.unknown.zextOrTrunc(width);
  if (!fourState) {
    value &= ~unknown;
    unknown.clearAllBits();
  }
  return {std::move(value), std::move(unknown)};
}

// False=0, true=1, ambiguous=2. A known one controls logical conversion.
inline unsigned vpiConstantTruth(const VPIConstantValue &input) {
  if (!(input.value & ~input.unknown).isZero())
    return 1;
  return input.unknown.isZero() ? 0 : 2;
}

// Evaluate only pure, fully typed integral operators. Declaration lookup and
// graph constancy are separate proofs; this helper never constructs or runs IR.
inline std::optional<VPIConstantValue> evaluateVPIIntegralConstant(
    uint32_t code, reflection::VPIIntegralExpressionShape result,
    llvm::ArrayRef<VPIConstantValue> values,
    llvm::ArrayRef<reflection::VPIIntegralExpressionShape> shapes) {
  using llvm::APInt;
  using Rule = reflection::VPIIntegralOperatorRule;
  if (values.size() != shapes.size() || result.width > UINT32_MAX ||
      !reflection::validVPIIntegralOperator(code, result, shapes.data(),
                                            shapes.size()))
    return std::nullopt;
  for (size_t index = 0; index != values.size(); ++index)
    if (values[index].value.getBitWidth() != shapes[index].width ||
        values[index].unknown.getBitWidth() != shapes[index].width)
      return std::nullopt;
  const unsigned width = result.width;
  const Rule rule = reflection::integralOperatorRule(code);
  if (rule == Rule::Cast)
    return resizeVPIConstant(values[0], width, shapes[0].isSigned,
                             result.fourState);
  auto boolean = [&](unsigned truth) -> VPIConstantValue {
    return {APInt(1, truth == 1), APInt(1, truth == 2 && result.fourState)};
  };
  auto allX = [&]() -> VPIConstantValue {
    return {APInt::getZero(width), result.fourState ? APInt::getAllOnes(width)
                                                    : APInt::getZero(width)};
  };
  auto operand = [&](unsigned index) {
    return resizeVPIConstant(values[index], width, result.isSigned,
                             result.fourState);
  };
  if (rule == Rule::Reduction) {
    const auto &input = values.front();
    if (code == 3) {
      unsigned truth = vpiConstantTruth(input);
      return boolean(truth == 2 ? 2 : !truth);
    }
    bool zero = !(~input.value & ~input.unknown).isZero();
    bool one = !(input.value & ~input.unknown).isZero();
    bool unknown = !input.unknown.isZero();
    unsigned truth = code <= 6   ? (zero      ? 0
                                    : unknown ? 2
                                              : 1)
                     : code <= 8 ? (one       ? 1
                                    : unknown ? 2
                                              : 0)
                                 : (unknown ? 2 : input.value.popcount() % 2);
    if ((code == 6 || code == 8 || code == 10) && truth != 2)
      truth = !truth;
    return boolean(truth);
  }
  if (rule == Rule::Logical) {
    unsigned left = vpiConstantTruth(values[0]);
    unsigned right = vpiConstantTruth(values[1]);
    if (code == 50 && left != 2)
      left = !left;
    return boolean(code == 26 ? (!left || !right           ? 0
                                 : left == 2 || right == 2 ? 2
                                                           : 1)
                              : (left == 1 || right == 1   ? 1
                                 : left == 2 || right == 2 ? 2
                                                           : 0));
  }
  if (rule == Rule::Comparison) {
    unsigned commonWidth = std::max(shapes[0].width, shapes[1].width);
    bool sign = shapes[0].isSigned && shapes[1].isSigned;
    bool state = shapes[0].fourState || shapes[1].fourState;
    auto left = resizeVPIConstant(values[0], commonWidth, sign, state);
    auto right = resizeVPIConstant(values[1], commonWidth, sign, state);
    if (code == 16 || code == 17) {
      bool equal = left.value == right.value && left.unknown == right.unknown;
      return boolean(code == 16 ? equal : !equal);
    }
    if (code == 14 || code == 15 || code == 69 || code == 70) {
      APInt mask = code >= 69 ? ~right.unknown : APInt::getAllOnes(commonWidth);
      APInt unknown = left.unknown | right.unknown;
      bool mismatch = !((left.value ^ right.value) & ~unknown & mask).isZero();
      bool ambiguous = !(unknown & mask).isZero();
      bool unequal = code == 15 || code == 70;
      return boolean(mismatch    ? unsigned(unequal)
                     : ambiguous ? 2
                                 : unsigned(!unequal));
    }
    if (!left.unknown.isZero() || !right.unknown.isZero())
      return boolean(2);
    int compared =
        left.value == right.value ? 0
        : (sign ? left.value.slt(right.value) : left.value.ult(right.value))
            ? -1
            : 1;
    return boolean(code == 18   ? compared > 0
                   : code == 19 ? compared >= 0
                   : code == 20 ? compared < 0
                                : compared <= 0);
  }
  if (rule == Rule::Conditional) {
    unsigned condition = vpiConstantTruth(values[0]);
    if (condition != 2)
      return operand(condition ? 1 : 2);
    auto left = operand(1), right = operand(2);
    APInt unknown = left.unknown | right.unknown | (left.value ^ right.value);
    left.value &= ~unknown;
    left.unknown = result.fourState ? unknown : APInt::getZero(width);
    return left;
  }
  auto left = operand(0);
  if (rule == Rule::Unary) {
    if (code == 2)
      return left;
    if (code == 4) {
      left.value = ~left.value & ~left.unknown;
      return left;
    }
    if (!left.unknown.isZero())
      return allX();
    left.value = -left.value;
    return left;
  }
  if (rule == Rule::Left) {
    const auto &amount = values[1];
    if (!amount.unknown.isZero())
      return allX();
    if (code != 43) {
      unsigned shift = amount.value.getLimitedValue(width);
      if (code == 22 || code == 41) {
        left.value <<= shift;
        left.unknown <<= shift;
      } else if (code == 42 && result.isSigned) {
        left.value = left.value.ashr(shift);
        left.unknown = left.unknown.ashr(shift);
      } else {
        left.value = left.value.lshr(shift);
        left.unknown = left.unknown.lshr(shift);
      }
      return left;
    }
    if (!left.unknown.isZero())
      return allX();
    if (shapes[1].isSigned && amount.value.isNegative()) {
      if (left.value.isZero())
        return allX();
      APInt answer(width, 0);
      if (left.value.isOne())
        answer = APInt(width, 1);
      else if (result.isSigned && left.value.isAllOnes())
        answer = amount.value[0] ? APInt::getAllOnes(width) : APInt(width, 1);
      return VPIConstantValue{std::move(answer), APInt::getZero(width)};
    }
    APInt answer(width, 1), factor = left.value, exponent = amount.value;
    while (!exponent.isZero()) {
      if (exponent[0])
        answer *= factor;
      exponent.lshrInPlace(1);
      if (!exponent.isZero())
        factor *= factor;
    }
    return VPIConstantValue{std::move(answer), APInt::getZero(width)};
  }
  auto right = operand(1);
  if (code >= 28 && code <= 31) {
    APInt knownLeft = ~left.unknown, knownRight = ~right.unknown;
    if (code == 28) {
      APInt zero = (~left.value & knownLeft) | (~right.value & knownRight);
      APInt one = (left.value & knownLeft) & (right.value & knownRight);
      return VPIConstantValue{one, ~(zero | one)};
    }
    if (code == 29) {
      APInt one = (left.value & knownLeft) | (right.value & knownRight);
      APInt zero = (~left.value & knownLeft) & (~right.value & knownRight);
      return VPIConstantValue{one, ~(zero | one)};
    }
    APInt unknown = left.unknown | right.unknown;
    APInt bits = left.value ^ right.value;
    if (code == 31)
      bits = ~bits;
    return VPIConstantValue{bits & ~unknown, unknown};
  }
  if (!left.unknown.isZero() || !right.unknown.isZero())
    return allX();
  switch (code) {
  case 11:
    left.value -= right.value;
    break;
  case 24:
    left.value += right.value;
    break;
  case 25:
    left.value *= right.value;
    break;
  case 12:
  case 13:
    if (right.value.isZero())
      return allX();
    if (result.isSigned && left.value.isMinSignedValue() &&
        right.value.isAllOnes()) {
      if (code == 13)
        left.value.clearAllBits();
    } else if (result.isSigned) {
      left.value = code == 12 ? left.value.sdiv(right.value)
                              : left.value.srem(right.value);
    } else {
      left.value = code == 12 ? left.value.udiv(right.value)
                              : left.value.urem(right.value);
    }
    break;
  default:
    return std::nullopt;
  }
  return left;
}

inline std::optional<int64_t>
knownVPIConstantInteger(const VPIConstantValue &input, bool isSigned) {
  if (!input.unknown.isZero())
    return std::nullopt;
  if (isSigned) {
    if (!input.value.isSignedIntN(64))
      return std::nullopt;
    return input.value.sextOrTrunc(64).getSExtValue();
  }
  if (input.value.getActiveBits() > 63)
    return std::nullopt;
  return static_cast<int64_t>(input.value.getZExtValue());
}

} // namespace obelisk::sim

#endif
