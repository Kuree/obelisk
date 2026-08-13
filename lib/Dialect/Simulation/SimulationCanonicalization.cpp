//===- SimulationCanonicalization.cpp - Folding and canonicalization ===//
//
// Constant folding for the logic ops and the rewrite patterns registered as
// canonicalizations for logic, aggregate, and reference view operations.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::sim {

namespace {

struct LogicPlanes {
  APInt value;
  APInt unknown;
};

/// Decode the dialect's folded representation of a four-state value.
static std::optional<LogicPlanes> getLogicPlanes(Attribute attribute) {
  auto planes = dyn_cast_or_null<ArrayAttr>(attribute);
  if (!planes || planes.size() != 2)
    return std::nullopt;
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown ||
      value.getValue().getBitWidth() != unknown.getValue().getBitWidth())
    return std::nullopt;
  return LogicPlanes{value.getValue(), unknown.getValue()};
}

static ArrayAttr getLogicAttribute(MLIRContext *context, LogicPlanes planes) {
  auto type = IntegerType::get(context, planes.value.getBitWidth());
  return ArrayAttr::get(context, {IntegerAttr::get(type, planes.value),
                                  IntegerAttr::get(type, planes.unknown)});
}

static LogicPlanes getCanonicalUnknown(unsigned width) {
  return {APInt::getZero(width), APInt::getAllOnes(width)};
}

static LogicPlanes getLogicBoolean(bool value, bool unknown = false) {
  return {APInt(1, value), APInt(1, unknown)};
}

struct TruthState {
  bool value;
  bool unknown;
};

static TruthState getTruth(LogicPlanes input) {
  bool value = !(input.value & ~input.unknown).isZero();
  return {value, !value && !input.unknown.isZero()};
}

/// A present ConstantIndex is a constant operand. `value` is absent exactly
/// when that constant is a four-state value containing X or Z.
struct ConstantIndex {
  std::optional<APInt> value;
};

static std::optional<ConstantIndex> getConstantIndex(Attribute attribute) {
  if (auto integer = dyn_cast_or_null<IntegerAttr>(attribute))
    return ConstantIndex{integer.getValue()};
  auto planes = getLogicPlanes(attribute);
  if (!planes)
    return std::nullopt;
  if (!planes->unknown.isZero())
    return ConstantIndex{std::nullopt};
  return ConstantIndex{planes->value};
}

static std::optional<ConstantIndex> getConstantIndex(Value value) {
  Attribute attribute;
  if (!matchPattern(value, m_Constant(&attribute)))
    return std::nullopt;
  return getConstantIndex(attribute);
}

/// Interpret `index` as a signed two's-complement low bit and select one plane.
/// Arbitrary-width indices are classified against the input bounds before any
/// narrowing to a host integer.
static APInt dynamicExtractPlane(const APInt &input, const APInt &index,
                                 unsigned resultWidth, bool invalidOne) {
  APInt result(resultWidth, 0);
  if (invalidOne)
    result.setAllBits();

  if (index.isNegative()) {
    APInt magnitude = -index;
    if (magnitude.uge(resultWidth))
      return result;
    if (!magnitude.isIntN(64))
      return result;
    uint64_t skipped = magnitude.getZExtValue();
    for (uint64_t resultBit = skipped; resultBit < resultWidth; ++resultBit) {
      if (input[resultBit - skipped])
        result.setBit(resultBit);
      else
        result.clearBit(resultBit);
    }
    return result;
  }

  if (index.uge(input.getBitWidth()))
    return result;
  if (!index.isIntN(64))
    return result;
  uint64_t low = index.getZExtValue();
  for (uint64_t resultBit = 0; resultBit < resultWidth; ++resultBit) {
    uint64_t inputBit = low + resultBit;
    if (inputBit >= input.getBitWidth())
      break;
    if (input[inputBit])
      result.setBit(resultBit);
    else
      result.clearBit(resultBit);
  }
  return result;
}

static LogicPlanes dynamicExtract(LogicPlanes input, const APInt &index,
                                  unsigned resultWidth) {
  return {dynamicExtractPlane(input.value, index, resultWidth, false),
          dynamicExtractPlane(input.unknown, index, resultWidth, true)};
}

/// Interpret `index` as a signed two's-complement low bit and replace the
/// overlapping portion of one packed plane. Invalid and nonoverlapping
/// selections leave the input unchanged.
static APInt dynamicInsertPlane(const APInt &input, const APInt &replacement,
                                const APInt &index) {
  APInt result = input;
  if (index.isNegative()) {
    APInt magnitude = -index;
    if (magnitude.uge(replacement.getBitWidth()) || !magnitude.isIntN(64))
      return result;
    uint64_t skipped = magnitude.getZExtValue();
    for (uint64_t replacementBit = skipped;
         replacementBit < replacement.getBitWidth(); ++replacementBit) {
      uint64_t inputBit = replacementBit - skipped;
      if (inputBit >= input.getBitWidth())
        break;
      result.setBitVal(inputBit, replacement[replacementBit]);
    }
    return result;
  }

  if (index.uge(input.getBitWidth()) || !index.isIntN(64))
    return result;
  uint64_t low = index.getZExtValue();
  for (uint64_t replacementBit = 0;
       replacementBit < replacement.getBitWidth(); ++replacementBit) {
    uint64_t inputBit = low + replacementBit;
    if (inputBit >= input.getBitWidth())
      break;
    result.setBitVal(inputBit, replacement[replacementBit]);
  }
  return result;
}

static bool isKnownInRangeIndex(const APInt &index, uint64_t inputWidth,
                                uint64_t resultWidth, uint64_t &low) {
  if (index.isNegative() || index.uge(inputWidth))
    return false;
  if (!index.isIntN(64))
    return false;
  low = index.getZExtValue();
  return resultWidth <= inputWidth - low;
}

} // namespace

LogicalResult SimLogicConstantOp::verify() {
  unsigned width = getResult().getType().getWidth();
  if (getValue().getBitWidth() != width || getUnknown().getBitWidth() != width)
    return emitOpError("value and unknown planes must match result width");
  return success();
}
OpFoldResult SimLogicConstantOp::fold(FoldAdaptor adaptor) {
  return ArrayAttr::get(getContext(),
                        {adaptor.getValueAttr(), adaptor.getUnknownAttr()});
}

OpFoldResult SimLogicFromBitsOp::fold(FoldAdaptor adaptor) {
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(
      getContext(),
      {input.getValue(), APInt::getZero(input.getValue().getBitWidth())});
}

LogicalResult SimLogicFromBitsOp::verify() {
  if (!getInput().getType().isSignless())
    return emitOpError("input must be a signless builtin integer");
  if (getInput().getType().getWidth() != getResult().getType().getWidth())
    return emitOpError("input and result widths must match");
  return success();
}
LogicalResult SimLogicToBitsOp::verify() {
  if (!getResult().getType().isSignless())
    return emitOpError("result must be a signless builtin integer");
  if (getInput().getType().getWidth() != getResult().getType().getWidth())
    return emitOpError("input and result widths must match");
  return success();
}

OpFoldResult SimLogicToBitsOp::fold(FoldAdaptor adaptor) {
  // to_bits(from_bits(x)) is x. The reverse is not an identity, because
  // from_bits discards the unknown plane it cannot represent.
  if (auto fromBits = getInput().getDefiningOp<SimLogicFromBitsOp>())
    return fromBits.getInput();
  auto planes = dyn_cast_or_null<ArrayAttr>(adaptor.getInput());
  if (!planes || planes.size() != 2)
    return {};
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown)
    return {};
  APInt converted = value.getValue() & ~unknown.getValue();
  return IntegerAttr::get(getResult().getType(), converted);
}

LogicalResult SimLogicCountBitsOp::verify() {
  if (getControls().empty())
    return emitOpError("requires at least one state control");
  std::optional<uint64_t> width;
  if (auto packed = getPackedWidth(getInput().getType()))
    width = *packed;
  else
    width = getProvenanceSpan(getInput().getType());
  if (!width || *width == 0)
    return emitOpError("input must be a nonempty fixed bitstream value");
  return success();
}

OpFoldResult SimLogicCountBitsOp::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  bool selected[4] = {};
  for (Attribute attribute : adaptor.getControls()) {
    auto control = getLogicPlanes(attribute);
    if (!control)
      return {};
    unsigned state =
        (control->unknown[0] ? 2u : 0u) | (control->value[0] ? 1u : 0u);
    selected[state] = true;
  }

  APInt value = input->value;
  APInt unknown = input->unknown;
  APInt known = ~unknown;
  APInt matches = APInt::getZero(value.getBitWidth());
  if (selected[0])
    matches |= ~value & known;
  if (selected[1])
    matches |= value & known;
  if (selected[2])
    matches |= ~value & unknown;
  if (selected[3])
    matches |= value & unknown;
  return IntegerAttr::get(IntegerType::get(getContext(), 32),
                          APInt(32, matches.popcount()));
}

OpFoldResult SimLogicClog2Op::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  APInt value = input->value & ~input->unknown;
  uint64_t result = value.isZero() ? 0 : (value - 1).getActiveBits();
  return IntegerAttr::get(IntegerType::get(getContext(), 32),
                          APInt(32, result));
}

OpFoldResult SimLogicIsTrueOp::fold(FoldAdaptor adaptor) {
  auto planes = dyn_cast_or_null<ArrayAttr>(adaptor.getInput());
  if (!planes || planes.size() != 2)
    return {};
  auto value = dyn_cast<IntegerAttr>(planes[0]);
  auto unknown = dyn_cast<IntegerAttr>(planes[1]);
  if (!value || !unknown)
    return {};
  bool isTrue = !(value.getValue() & ~unknown.getValue()).isZero();
  return IntegerAttr::get(getResult().getType(), isTrue ? 1 : 0);
}

OpFoldResult SimLogicMuxOp::fold(FoldAdaptor adaptor) {
  if (getTrueValue() == getFalseValue())
    return getTrueValue();
  auto condition = getLogicPlanes(adaptor.getCondition());
  if (!condition)
    return {};
  if (condition->unknown.isZero())
    return condition->value.isZero() ? OpFoldResult(getFalseValue())
                                     : OpFoldResult(getTrueValue());
  auto trueValue = getLogicPlanes(adaptor.getTrueValue());
  auto falseValue = getLogicPlanes(adaptor.getFalseValue());
  if (!trueValue || !falseValue)
    return {};
  APInt mismatch = (trueValue->value ^ falseValue->value) |
                   (trueValue->unknown ^ falseValue->unknown);
  LogicPlanes result{trueValue->value & ~mismatch,
                     trueValue->unknown | mismatch};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicResizeOp::fold(FoldAdaptor adaptor) {
  if (getInput().getType() == getResult().getType())
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  unsigned width = getResult().getType().getWidth();
  LogicPlanes result = getIsSigned()
                           ? LogicPlanes{input->value.sextOrTrunc(width),
                                         input->unknown.sextOrTrunc(width)}
                           : LogicPlanes{input->value.zextOrTrunc(width),
                                         input->unknown.zextOrTrunc(width)};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicUnaryOp::fold(FoldAdaptor adaptor) {
  if (getKind() == UnaryKind::Plus)
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  LogicPlanes result = *input;
  switch (getKind()) {
  case UnaryKind::Plus:
    llvm_unreachable("unary plus folded above");
  case UnaryKind::Negate:
    result = input->unknown.isZero()
                 ? LogicPlanes{-input->value,
                               APInt::getZero(input->value.getBitWidth())}
                 : getCanonicalUnknown(input->value.getBitWidth());
    break;
  case UnaryKind::BitNot:
    result = {~input->value & ~input->unknown, input->unknown};
    break;
  case UnaryKind::LogicalNot: {
    TruthState truth = getTruth(*input);
    result = getLogicBoolean(!truth.value && !truth.unknown, truth.unknown);
    break;
  }
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicReductionOp::fold(FoldAdaptor adaptor) {
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};

  bool hasUnknown = !input->unknown.isZero();
  bool value = false;
  bool unknown = false;
  bool invert = getKind() == ReductionKind::Nand ||
                getKind() == ReductionKind::Nor ||
                getKind() == ReductionKind::Xnor;
  if (getKind() == ReductionKind::And || getKind() == ReductionKind::Nand) {
    bool hasKnownZero = !(~input->value & ~input->unknown).isZero();
    unknown = !hasKnownZero && hasUnknown;
    value = !hasKnownZero && !hasUnknown;
  } else if (getKind() == ReductionKind::Or ||
             getKind() == ReductionKind::Nor) {
    bool hasKnownOne = !(input->value & ~input->unknown).isZero();
    unknown = !hasKnownOne && hasUnknown;
    value = hasKnownOne;
  } else {
    unknown = hasUnknown;
    value = !hasUnknown && input->value.popcount() % 2;
  }
  if (invert && !unknown)
    value = !value;
  return getLogicAttribute(getContext(), getLogicBoolean(value, unknown));
}

LogicalResult SimLogicUnaryOp::verify() {
  if (getKind() == UnaryKind::LogicalNot) {
    if (getResult().getType().getWidth() != 1)
      return emitOpError("logical negation must produce !obelisk_sim.logic<1>");
  } else if (getInput().getType() != getResult().getType()) {
    return emitOpError("width-preserving unary operations require matching "
                       "input and result types");
  }
  return success();
}

OpFoldResult SimLogicBinaryOp::fold(FoldAdaptor adaptor) {
  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());

  // These are controlling values, not identities: unlike `x & all_ones` and
  // `x | zero`, they are insensitive to whether unknown bits encode X or Z.
  auto isKnownZero = [](const std::optional<LogicPlanes> &planes) {
    return planes && planes->unknown.isZero() && planes->value.isZero();
  };
  auto isKnownOnes = [](const std::optional<LogicPlanes> &planes) {
    return planes && planes->unknown.isZero() && planes->value.isAllOnes();
  };
  unsigned width = getResult().getType().getWidth();
  if (getKind() == BinaryKind::And && (isKnownZero(lhs) || isKnownZero(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});
  if (getKind() == BinaryKind::Or && (isKnownOnes(lhs) || isKnownOnes(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getAllOnes(width), APInt::getZero(width)});
  if (!lhs || !rhs)
    return {};

  LogicPlanes result{APInt::getZero(width), APInt::getZero(width)};
  if (getKind() == BinaryKind::And || getKind() == BinaryKind::Or ||
      getKind() == BinaryKind::Xor || getKind() == BinaryKind::Xnor) {
    APInt lhsKnown = ~lhs->unknown;
    APInt rhsKnown = ~rhs->unknown;
    if (getKind() == BinaryKind::And) {
      APInt knownZero = (~lhs->value & lhsKnown) | (~rhs->value & rhsKnown);
      APInt knownOne = (lhs->value & lhsKnown) & (rhs->value & rhsKnown);
      result = {knownOne, ~(knownZero | knownOne)};
    } else if (getKind() == BinaryKind::Or) {
      APInt knownOne = (lhs->value & lhsKnown) | (rhs->value & rhsKnown);
      APInt knownZero = (~lhs->value & lhsKnown) & (~rhs->value & rhsKnown);
      result = {knownOne, ~(knownZero | knownOne)};
    } else {
      result.unknown = lhs->unknown | rhs->unknown;
      APInt computed = lhs->value ^ rhs->value;
      if (getKind() == BinaryKind::Xnor)
        computed = ~computed;
      result.value = computed & ~result.unknown;
    }
    return getLogicAttribute(getContext(), std::move(result));
  }

  if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
    return getLogicAttribute(getContext(), getCanonicalUnknown(width));

  bool invalid = false;
  switch (getKind()) {
  case BinaryKind::Add:
    result.value = lhs->value + rhs->value;
    break;
  case BinaryKind::Sub:
    result.value = lhs->value - rhs->value;
    break;
  case BinaryKind::Mul:
    result.value = lhs->value * rhs->value;
    break;
  case BinaryKind::UDiv:
  case BinaryKind::SDiv:
  case BinaryKind::UMod:
  case BinaryKind::SMod: {
    if (rhs->value.isZero()) {
      invalid = true;
      break;
    }
    bool isSigned =
        getKind() == BinaryKind::SDiv || getKind() == BinaryKind::SMod;
    bool isRemainder =
        getKind() == BinaryKind::UMod || getKind() == BinaryKind::SMod;
    bool overflow =
        isSigned && lhs->value.isMinSignedValue() && rhs->value.isAllOnes();
    if (overflow) {
      result.value = isRemainder ? APInt::getZero(width) : lhs->value;
    } else if (isSigned && isRemainder) {
      result.value = lhs->value.srem(rhs->value);
    } else if (isSigned) {
      result.value = lhs->value.sdiv(rhs->value);
    } else if (isRemainder) {
      result.value = lhs->value.urem(rhs->value);
    } else {
      result.value = lhs->value.udiv(rhs->value);
    }
    break;
  }
  default:
    llvm_unreachable("bitwise binary kinds folded above");
  }
  if (invalid)
    result = getCanonicalUnknown(width);
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicLogicalOp::fold(FoldAdaptor adaptor) {
  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());

  // Only controlling truth constants are identities in the presence of Z.
  if (lhs) {
    TruthState truth = getTruth(*lhs);
    if (getKind() == LogicalKind::And && !truth.value && !truth.unknown)
      return getLogicAttribute(getContext(), getLogicBoolean(false));
    if (getKind() == LogicalKind::Or && truth.value)
      return getLogicAttribute(getContext(), getLogicBoolean(true));
  }
  if (rhs) {
    TruthState truth = getTruth(*rhs);
    if (getKind() == LogicalKind::And && !truth.value && !truth.unknown)
      return getLogicAttribute(getContext(), getLogicBoolean(false));
    if (getKind() == LogicalKind::Or && truth.value)
      return getLogicAttribute(getContext(), getLogicBoolean(true));
  }
  if (!lhs || !rhs)
    return {};

  TruthState lhsTruth = getTruth(*lhs);
  TruthState rhsTruth = getTruth(*rhs);
  bool value;
  bool unknown;
  if (getKind() == LogicalKind::And) {
    value = lhsTruth.value && rhsTruth.value;
    bool knownFalse = (!lhsTruth.value && !lhsTruth.unknown) ||
                      (!rhsTruth.value && !rhsTruth.unknown);
    unknown = !knownFalse && !value;
  } else {
    value = lhsTruth.value || rhsTruth.value;
    bool knownFalse = !lhsTruth.value && !lhsTruth.unknown && !rhsTruth.value &&
                      !rhsTruth.unknown;
    unknown = !knownFalse && !value;
  }
  return getLogicAttribute(getContext(), getLogicBoolean(value, unknown));
}

OpFoldResult SimLogicShiftOp::fold(FoldAdaptor adaptor) {
  auto amount = getConstantIndex(adaptor.getAmount());
  if (!amount)
    return {};
  unsigned width = getInput().getType().getWidth();
  if (!amount->value)
    return getLogicAttribute(getContext(), getCanonicalUnknown(width));
  if (amount->value->isZero())
    return getInput();

  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  if (amount->value->uge(width)) {
    if (getKind() == ShiftKind::RightArith)
      return getLogicAttribute(getContext(), {input->value.ashr(width - 1),
                                              input->unknown.ashr(width - 1)});
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});
  }

  uint64_t shift = amount->value->getZExtValue();
  LogicPlanes result = *input;
  if (getKind() == ShiftKind::Left)
    result = {input->value.shl(shift), input->unknown.shl(shift)};
  else if (getKind() == ShiftKind::Right)
    result = {input->value.lshr(shift), input->unknown.lshr(shift)};
  else
    result = {input->value.ashr(shift), input->unknown.ashr(shift)};
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicCompareOp::fold(FoldAdaptor adaptor) {
  bool integerResult =
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq;
  bool deterministic = integerResult;
  if (deterministic && getLhs() == getRhs()) {
    bool equal =
        getKind() != CompareKind::CaseNe && getKind() != CompareKind::WildNe;
    if (integerResult)
      return IntegerAttr::get(getResult().getType(), equal);
    return getLogicAttribute(getContext(), getLogicBoolean(equal));
  }

  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());
  if (!lhs || !rhs)
    return {};
  if (getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe) {
    bool equal = lhs->value == rhs->value && lhs->unknown == rhs->unknown;
    if (getKind() == CompareKind::CaseNe)
      equal = !equal;
    return IntegerAttr::get(getResult().getType(), equal);
  }
  if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq) {
    APInt wildcard = APInt::getZero(lhs->value.getBitWidth());
    if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe)
      wildcard = rhs->unknown;
    else if (getKind() == CompareKind::CaseZEq)
      wildcard = (lhs->unknown & lhs->value) | (rhs->unknown & rhs->value);
    else
      wildcard = lhs->unknown | rhs->unknown;
    APInt mismatch;
    APInt relevantUnknown = APInt::getZero(lhs->value.getBitWidth());
    if (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe) {
      APInt compared = ~rhs->unknown;
      mismatch = (lhs->value ^ rhs->value) & ~lhs->unknown & compared;
      relevantUnknown = lhs->unknown & compared;
    } else {
      mismatch = ((lhs->value ^ rhs->value) | (lhs->unknown ^ rhs->unknown)) &
                 ~wildcard;
    }
    bool equal = mismatch.isZero() && relevantUnknown.isZero();
    bool unknown = mismatch.isZero() && !relevantUnknown.isZero();
    if (getKind() == CompareKind::WildNe && !unknown)
      equal = !equal;
    if (integerResult)
      return IntegerAttr::get(getResult().getType(), equal);
    return getLogicAttribute(getContext(), getLogicBoolean(equal, unknown));
  }
  if (getKind() == CompareKind::Eq || getKind() == CompareKind::Ne) {
    APInt knownMask = ~(lhs->unknown | rhs->unknown);
    bool knownMismatch = !((lhs->value ^ rhs->value) & knownMask).isZero();
    if (knownMismatch)
      return getLogicAttribute(getContext(),
                               getLogicBoolean(getKind() == CompareKind::Ne));
    if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
      return getLogicAttribute(getContext(), getLogicBoolean(false, true));
    return getLogicAttribute(getContext(),
                             getLogicBoolean(getKind() == CompareKind::Eq));
  }
  if (!lhs->unknown.isZero() || !rhs->unknown.isZero())
    return getLogicAttribute(getContext(), getLogicBoolean(false, true));

  bool result;
  switch (getKind()) {
  case CompareKind::Eq:
    result = lhs->value == rhs->value;
    break;
  case CompareKind::Ne:
    result = lhs->value != rhs->value;
    break;
  case CompareKind::ULT:
    result = lhs->value.ult(rhs->value);
    break;
  case CompareKind::ULE:
    result = lhs->value.ule(rhs->value);
    break;
  case CompareKind::UGT:
    result = lhs->value.ugt(rhs->value);
    break;
  case CompareKind::UGE:
    result = lhs->value.uge(rhs->value);
    break;
  case CompareKind::SLT:
    result = lhs->value.slt(rhs->value);
    break;
  case CompareKind::SLE:
    result = lhs->value.sle(rhs->value);
    break;
  case CompareKind::SGT:
    result = lhs->value.sgt(rhs->value);
    break;
  case CompareKind::SGE:
    result = lhs->value.sge(rhs->value);
    break;
  default:
    llvm_unreachable("case comparisons folded above");
  }
  return getLogicAttribute(getContext(), getLogicBoolean(result));
}

LogicalResult SimLogicCompareOp::verify() {
  Type result = getResult().getType();
  bool caseComparison =
      getKind() == CompareKind::CaseEq || getKind() == CompareKind::CaseNe ||
      getKind() == CompareKind::CaseZEq || getKind() == CompareKind::CaseXZEq;
  if (caseComparison && !result.isSignlessInteger(1))
    return emitOpError("case comparisons must produce i1");
  if (!caseComparison && !isa<LogicType>(result))
    return emitOpError(
        "four-state comparisons must produce !obelisk_sim.logic<1>");
  if (auto logic = dyn_cast<LogicType>(result); logic && logic.getWidth() != 1)
    return emitOpError("comparison result logic width must be one");
  return success();
}
LogicalResult SimLogicShiftOp::verify() {
  if (!isa<IntegerType, LogicType>(getAmount().getType()))
    return emitOpError("shift amount must be an integer or four-state logic");
  return success();
}

OpFoldResult SimLogicConcatOp::fold(FoldAdaptor adaptor) {
  if (getInputs().size() == 1)
    return getInputs().front();
  unsigned resultWidth = getResult().getType().getWidth();
  LogicPlanes result{APInt::getZero(resultWidth), APInt::getZero(resultWidth)};
  uint64_t offset = resultWidth;
  for (Attribute attribute : adaptor.getInputs()) {
    auto input = getLogicPlanes(attribute);
    if (!input)
      return {};
    offset -= input->value.getBitWidth();
    result.value |= input->value.zextOrTrunc(resultWidth).shl(offset);
    result.unknown |= input->unknown.zextOrTrunc(resultWidth).shl(offset);
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicReplicateOp::fold(FoldAdaptor adaptor) {
  if (getCount() == 1)
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  unsigned inputWidth = input->value.getBitWidth();
  LogicPlanes result{APInt::getZero(resultWidth), APInt::getZero(resultWidth)};
  LogicPlanes chunk{input->value.zextOrTrunc(resultWidth),
                    input->unknown.zextOrTrunc(resultWidth)};
  uint64_t remaining = static_cast<uint64_t>(getCount());
  uint64_t chunkCopies = 1;
  uint64_t placedCopies = 0;
  while (remaining != 0) {
    if (remaining & 1) {
      uint64_t offset = placedCopies * static_cast<uint64_t>(inputWidth);
      result.value |= chunk.value.shl(offset);
      result.unknown |= chunk.unknown.shl(offset);
      placedCopies += chunkCopies;
    }
    remaining >>= 1;
    if (remaining == 0)
      break;
    uint64_t chunkWidth = chunkCopies * static_cast<uint64_t>(inputWidth);
    chunk.value |= chunk.value.shl(chunkWidth);
    chunk.unknown |= chunk.unknown.shl(chunkWidth);
    chunkCopies <<= 1;
  }
  return getLogicAttribute(getContext(), std::move(result));
}

OpFoldResult SimLogicExtractOp::fold(FoldAdaptor adaptor) {
  uint64_t low = getLowBit();
  unsigned resultWidth = getResult().getType().getWidth();
  if (low == 0 && resultWidth == getInput().getType().getWidth())
    return getInput();
  if (auto insert = getInput().getDefiningOp<SimLogicInsertOp>();
      insert && insert.getLowBit() == low &&
      insert.getReplacement().getType().getWidth() == resultWidth)
    return insert.getReplacement();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(getContext(),
                           {input->value.lshr(low).trunc(resultWidth),
                            input->unknown.lshr(low).trunc(resultWidth)});
}

OpFoldResult SimLogicDynExtractOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  if (!index->value)
    return getLogicAttribute(getContext(), getCanonicalUnknown(resultWidth));
  if (index->value->isZero() && resultWidth == getInput().getType().getWidth())
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  return getLogicAttribute(getContext(),
                           dynamicExtract(*input, *index->value, resultWidth));
}

OpFoldResult SimBitsDynExtractOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  unsigned resultWidth = getResult().getType().getWidth();
  if (!index->value)
    return IntegerAttr::get(getResult().getType(), APInt::getZero(resultWidth));
  if (index->value->isZero() && resultWidth == getInput().getType().getWidth())
    return getInput();
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  if (!input)
    return {};
  APInt result =
      dynamicExtractPlane(input.getValue(), *index->value, resultWidth, false);
  return IntegerAttr::get(getResult().getType(), result);
}

OpFoldResult SimLogicDynInsertOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  if (!index->value)
    return getInput();
  auto input = getLogicPlanes(adaptor.getInput());
  auto replacement = getLogicPlanes(adaptor.getReplacement());
  if (!input || !replacement)
    return {};
  return getLogicAttribute(
      getContext(),
      {dynamicInsertPlane(input->value, replacement->value, *index->value),
       dynamicInsertPlane(input->unknown, replacement->unknown,
                          *index->value)});
}

OpFoldResult SimBitsDynInsertOp::fold(FoldAdaptor adaptor) {
  auto index = getConstantIndex(adaptor.getLowBit());
  if (!index)
    return {};
  if (!index->value)
    return getInput();
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  auto replacement =
      dyn_cast_or_null<IntegerAttr>(adaptor.getReplacement());
  if (!input || !replacement)
    return {};
  APInt result = dynamicInsertPlane(input.getValue(), replacement.getValue(),
                                    *index->value);
  return IntegerAttr::get(getResult().getType(), result);
}

OpFoldResult SimLogicInsertOp::fold(FoldAdaptor adaptor) {
  uint64_t low = getLowBit();
  unsigned width = getResult().getType().getWidth();
  unsigned replacementWidth = getReplacement().getType().getWidth();
  if (low == 0 && replacementWidth == width)
    return getReplacement();
  if (auto extract = getReplacement().getDefiningOp<SimLogicExtractOp>();
      extract && extract.getInput() == getInput() &&
      extract.getLowBit() == low &&
      extract.getResult().getType().getWidth() == replacementWidth)
    return getInput();

  auto input = getLogicPlanes(adaptor.getInput());
  auto replacement = getLogicPlanes(adaptor.getReplacement());
  if (!input || !replacement)
    return {};
  APInt mask = APInt::getLowBitsSet(width, replacementWidth).shl(low);
  auto insertPlane = [&](const APInt &base, const APInt &piece) {
    return (base & ~mask) | piece.zextOrTrunc(width).shl(low);
  };
  return getLogicAttribute(getContext(),
                           {insertPlane(input->value, replacement->value),
                            insertPlane(input->unknown, replacement->unknown)});
}

LogicalResult SimLogicConcatOp::verify() {
  if (getInputs().empty())
    return emitOpError("requires at least one input");
  uint64_t width = 0;
  for (Value input : getInputs())
    width += cast<LogicType>(input.getType()).getWidth();
  if (width != getResult().getType().getWidth())
    return emitOpError("result width must equal the sum of input widths");
  return success();
}
LogicalResult SimLogicReplicateOp::verify() {
  if (getCount() <= 0)
    return emitOpError("replication count must be positive");
  uint64_t count = static_cast<uint64_t>(getCount());
  uint64_t inputWidth = getInput().getType().getWidth();
  if (count > std::numeric_limits<uint64_t>::max() / inputWidth)
    return emitOpError("replication width overflows uint64_t");
  uint64_t expected = count * inputWidth;
  if (expected > std::numeric_limits<unsigned>::max())
    return emitOpError("replication width exceeds the supported type width");
  if (expected != getResult().getType().getWidth())
    return emitOpError("result width must equal input width times count");
  return success();
}
LogicalResult SimLogicExtractOp::verify() {
  if (getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() +
              getResult().getType().getWidth() >
          getInput().getType().getWidth())
    return emitOpError("constant selection is outside the input width");
  return success();
}
LogicalResult SimLogicDynExtractOp::verify() {
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getResult().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("result width exceeds input width");
  return success();
}
LogicalResult SimBitsDynExtractOp::verify() {
  if (!getInput().getType().isSignless() || !getResult().getType().isSignless())
    return emitOpError("input and result must be signless builtin integers");
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getResult().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("result width exceeds input width");
  return success();
}
LogicalResult SimLogicDynInsertOp::verify() {
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getReplacement().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("replacement width exceeds input width");
  return success();
}
LogicalResult SimBitsDynInsertOp::verify() {
  if (!getInput().getType().isSignless() ||
      !getReplacement().getType().isSignless() ||
      !getResult().getType().isSignless())
    return emitOpError("input, replacement, and result must be signless "
                       "builtin integers");
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())))
    return failure();
  if (getReplacement().getType().getWidth() > getInput().getType().getWidth())
    return emitOpError("replacement width exceeds input width");
  return success();
}
LogicalResult SimLogicInsertOp::verify() {
  if (getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() +
              getReplacement().getType().getWidth() >
          getInput().getType().getWidth())
    return emitOpError("replacement is outside the input width");
  return success();
}

namespace {

static std::optional<uint64_t> getSelectionWidth(Type type) {
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  if (auto reference = dyn_cast<RefType>(type))
    return getPackedWidth(reference.getElementType());
  if (auto driver = dyn_cast<DriverType>(type))
    return getPackedWidth(driver.getElementType());
  return std::nullopt;
}

template <typename OldOp, typename NewOp>
static void replaceWithNewOp(PatternRewriter &rewriter, OldOp oldOp,
                             NewOp newOp) {
  for (NamedAttribute attribute : oldOp->getDiscardableAttrDictionary())
    newOp->setAttr(attribute.getName(), attribute.getValue());
  rewriter.replaceOp(oldOp, newOp.getResult());
}

struct CollapseResizeChain final : OpRewritePattern<SimLogicResizeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicResizeOp op,
                                PatternRewriter &rewriter) const override {
    auto inner = op.getInput().getDefiningOp<SimLogicResizeOp>();
    if (!inner)
      return failure();

    unsigned sourceWidth = inner.getInput().getType().getWidth();
    unsigned innerWidth = inner.getResult().getType().getWidth();
    unsigned resultWidth = op.getResult().getType().getWidth();
    bool signedResult;
    if (resultWidth <= std::min(sourceWidth, innerWidth)) {
      // Both resizes only contribute a low-bit truncation.
      signedResult = false;
    } else if (innerWidth >= sourceWidth && resultWidth <= innerWidth) {
      // The result observes the extension performed by the inner resize.
      signedResult = inner.getIsSigned();
    } else if (innerWidth >= sourceWidth && resultWidth > innerWidth &&
               (!inner.getIsSigned() || op.getIsSigned())) {
      // Repeated zero-extension, or repeated sign-extension, composes.
      signedResult = inner.getIsSigned();
    } else {
      return failure();
    }

    auto replacement = SimLogicResizeOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inner.getInput(),
        rewriter.getBoolAttr(signedResult));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

static bool isConstantValue(Value value) {
  Attribute attribute;
  return matchPattern(value, m_Constant(&attribute));
}

struct NormalizeBinaryConstant final : OpRewritePattern<SimLogicBinaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicBinaryOp op,
                                PatternRewriter &rewriter) const override {
    if (!isConstantValue(op.getLhs()) || isConstantValue(op.getRhs()))
      return failure();
    switch (op.getKind()) {
    case BinaryKind::Add:
    case BinaryKind::Mul:
    case BinaryKind::And:
    case BinaryKind::Or:
    case BinaryKind::Xor:
    case BinaryKind::Xnor:
      break;
    default:
      return failure();
    }
    rewriter.modifyOpInPlace(op, [&] {
      SmallVector<Value, 2> operands{op.getRhs(), op.getLhs()};
      op->setOperands(operands);
    });
    return success();
  }
};

struct NormalizeCompareConstant final : OpRewritePattern<SimLogicCompareOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCompareOp op,
                                PatternRewriter &rewriter) const override {
    if (!isConstantValue(op.getLhs()) || isConstantValue(op.getRhs()))
      return failure();
    CompareKind kind = op.getKind();
    switch (op.getKind()) {
    case CompareKind::Eq:
    case CompareKind::Ne:
    case CompareKind::CaseEq:
    case CompareKind::CaseNe:
    case CompareKind::CaseZEq:
    case CompareKind::CaseXZEq:
      kind = op.getKind();
      break;
    case CompareKind::WildEq:
    case CompareKind::WildNe:
      return failure();
    case CompareKind::ULT:
      kind = CompareKind::UGT;
      break;
    case CompareKind::ULE:
      kind = CompareKind::UGE;
      break;
    case CompareKind::UGT:
      kind = CompareKind::ULT;
      break;
    case CompareKind::UGE:
      kind = CompareKind::ULE;
      break;
    case CompareKind::SLT:
      kind = CompareKind::SGT;
      break;
    case CompareKind::SLE:
      kind = CompareKind::SGE;
      break;
    case CompareKind::SGT:
      kind = CompareKind::SLT;
      break;
    case CompareKind::SGE:
      kind = CompareKind::SLE;
      break;
    }
    rewriter.modifyOpInPlace(op, [&] {
      SmallVector<Value, 2> operands{op.getRhs(), op.getLhs()};
      op->setOperands(operands);
      op.setKind(kind);
    });
    return success();
  }
};

struct FlattenConcat final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    SmallVector<Value> inputs;
    bool changed = false;
    for (Value input : op.getInputs()) {
      if (auto nested = input.getDefiningOp<SimLogicConcatOp>()) {
        inputs.append(nested.getInputs().begin(), nested.getInputs().end());
        changed = true;
      } else {
        inputs.push_back(input);
      }
    }
    if (!changed)
      return failure();
    auto replacement = SimLogicConcatOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inputs);
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct ReplicateRepeatedConcatInput final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getInputs().size() < 2)
      return failure();
    Value input = op.getInputs().front();
    if (!llvm::all_of(op.getInputs(),
                      [input](Value value) { return value == input; }))
      return failure();
    auto replacement = SimLogicReplicateOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), input,
        rewriter.getI64IntegerAttr(op.getInputs().size()));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct MergeAdjacentConcatExtracts final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    SmallVector<Value> inputs;
    bool changed = false;
    for (Value input : op.getInputs()) {
      auto right = input.getDefiningOp<SimLogicExtractOp>();
      auto left = inputs.empty()
                      ? SimLogicExtractOp{}
                      : inputs.back().getDefiningOp<SimLogicExtractOp>();
      if (!left || !right || left.getInput() != right.getInput() ||
          left.getLowBit() !=
              right.getLowBit() + right.getResult().getType().getWidth()) {
        inputs.push_back(input);
        continue;
      }
      uint64_t width = left.getResult().getType().getWidth() +
                       right.getResult().getType().getWidth();
      auto type = LogicType::get(op.getContext(), width);
      auto merged = SimLogicExtractOp::create(
          rewriter, op.getLoc(), type, right.getInput(),
          rewriter.getI64IntegerAttr(right.getLowBit()));
      inputs.back() = merged.getResult();
      changed = true;
    }
    if (!changed)
      return failure();
    if (inputs.size() == 1) {
      for (NamedAttribute attribute : op->getDiscardableAttrDictionary())
        inputs.front().getDefiningOp()->setAttr(attribute.getName(),
                                                attribute.getValue());
      rewriter.replaceOp(op, inputs.front());
      return success();
    }
    auto replacement = SimLogicConcatOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inputs);
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct CombineReplication final : OpRewritePattern<SimLogicReplicateOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReplicateOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().getDefiningOp<SimLogicReplicateOp>();
    if (!nested)
      return failure();
    uint64_t innerCount = nested.getCount();
    uint64_t outerCount = op.getCount();
    if (outerCount != 0 &&
        innerCount > std::numeric_limits<uint64_t>::max() / outerCount)
      return failure();
    uint64_t count = innerCount * outerCount;
    if (count > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement = SimLogicReplicateOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        rewriter.getI64IntegerAttr(count));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

template <typename ExtractOp>
struct SimplifyStaticExtract final : OpRewritePattern<ExtractOp> {
  using OpRewritePattern<ExtractOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto inputWidth = getSelectionWidth(op.getInput().getType());
    auto resultWidth = getSelectionWidth(op.getResult().getType());
    if (!inputWidth || !resultWidth)
      return failure();
    if (op.getLowBit() == 0 && *inputWidth == *resultWidth) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }

    auto nested = op.getInput().template getDefiningOp<ExtractOp>();
    if (!nested)
      return failure();
    uint64_t innerLow = nested.getLowBit();
    uint64_t outerLow = op.getLowBit();
    if (innerLow > std::numeric_limits<uint64_t>::max() - outerLow)
      return failure();
    uint64_t low = innerLow + outerLow;
    if (low > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement =
        ExtractOp::create(rewriter, op.getLoc(), op.getResult().getType(),
                          nested.getInput(), rewriter.getI64IntegerAttr(low));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct SimplifyLogicExtractSource final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    uint64_t low = op.getLowBit();
    uint64_t width = op.getResult().getType().getWidth();
    uint64_t high = low + width;

    if (auto insert = op.getInput().getDefiningOp<SimLogicInsertOp>()) {
      uint64_t insertLow = insert.getLowBit();
      uint64_t insertWidth = insert.getReplacement().getType().getWidth();
      uint64_t insertHigh = insertLow + insertWidth;
      Value source;
      uint64_t sourceLow;
      if (high <= insertLow || low >= insertHigh) {
        source = insert.getInput();
        sourceLow = low;
      } else if (low >= insertLow && high <= insertHigh) {
        source = insert.getReplacement();
        sourceLow = low - insertLow;
      } else {
        return failure();
      }
      auto replacement = SimLogicExtractOp::create(
          rewriter, op.getLoc(), op.getResult().getType(), source,
          rewriter.getI64IntegerAttr(sourceLow));
      replaceWithNewOp(rewriter, op, replacement);
      return success();
    }

    if (auto replicate = op.getInput().getDefiningOp<SimLogicReplicateOp>()) {
      uint64_t inputWidth = replicate.getInput().getType().getWidth();
      if (low / inputWidth == (high - 1) / inputWidth) {
        auto replacement = SimLogicExtractOp::create(
            rewriter, op.getLoc(), op.getResult().getType(),
            replicate.getInput(), rewriter.getI64IntegerAttr(low % inputWidth));
        replaceWithNewOp(rewriter, op, replacement);
        return success();
      }
    }

    auto concat = op.getInput().getDefiningOp<SimLogicConcatOp>();
    if (!concat)
      return failure();
    uint64_t inputLow = 0;
    for (Value input : llvm::reverse(concat.getInputs())) {
      uint64_t inputWidth = cast<LogicType>(input.getType()).getWidth();
      uint64_t inputHigh = inputLow + inputWidth;
      if (low >= inputLow && high <= inputHigh) {
        auto replacement = SimLogicExtractOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), input,
            rewriter.getI64IntegerAttr(low - inputLow));
        replaceWithNewOp(rewriter, op, replacement);
        return success();
      }
      inputLow = inputHigh;
    }
    return failure();
  }
};

template <typename DynamicOp, typename StaticOp>
struct ConstantDynamicExtract final : OpRewritePattern<DynamicOp> {
  using OpRewritePattern<DynamicOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(DynamicOp op,
                                PatternRewriter &rewriter) const override {
    auto index = getConstantIndex(op.getLowBit());
    if (!index || !index->value)
      return failure();
    auto inputWidth = getSelectionWidth(op.getInput().getType());
    auto resultWidth = getSelectionWidth(op.getResult().getType());
    if (!inputWidth || !resultWidth)
      return failure();
    uint64_t low;
    if (!isKnownInRangeIndex(*index->value, *inputWidth, *resultWidth, low) ||
        low > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement =
        StaticOp::create(rewriter, op.getLoc(), op.getResult().getType(),
                         op.getInput(), rewriter.getI64IntegerAttr(low));
    for (NamedAttribute attribute : op->getDiscardableAttrDictionary())
      replacement->setAttr(attribute.getName(), attribute.getValue());
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

template <typename DynamicOp, typename StaticOp>
struct ConstantDynamicInsert final : OpRewritePattern<DynamicOp> {
  using OpRewritePattern<DynamicOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(DynamicOp op,
                                PatternRewriter &rewriter) const override {
    auto index = getConstantIndex(op.getLowBit());
    if (!index || !index->value)
      return failure();
    uint64_t low;
    if (!isKnownInRangeIndex(*index->value,
                             op.getInput().getType().getWidth(),
                             op.getReplacement().getType().getWidth(), low) ||
        low > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return failure();
    auto replacement = StaticOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        op.getReplacement(), rewriter.getI64IntegerAttr(low));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

struct RemoveOverwrittenInsert final : OpRewritePattern<SimLogicInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicInsertOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().getDefiningOp<SimLogicInsertOp>();
    if (!nested)
      return failure();
    uint64_t nestedLow = nested.getLowBit();
    uint64_t nestedHigh =
        nestedLow + nested.getReplacement().getType().getWidth();
    uint64_t outerLow = op.getLowBit();
    uint64_t outerHigh = outerLow + op.getReplacement().getType().getWidth();
    if (outerLow > nestedLow || outerHigh < nestedHigh)
      return failure();
    auto replacement = SimLogicInsertOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        op.getReplacement(), op.getLowBitAttr());
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

static std::optional<int64_t> getConstantSourceIndex(Value value,
                                                     bool &unknown) {
  unknown = false;
  std::optional<ConstantIndex> constant = getConstantIndex(value);
  if (!constant)
    return std::nullopt;
  if (!constant->value) {
    unknown = true;
    return std::nullopt;
  }
  if (!constant->value->isSignedIntN(64))
    return std::nullopt;
  return constant->value->getSExtValue();
}

struct SimplifyAggregateExtract final
    : OpRewritePattern<SimAggregateExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateExtractOp op,
                                PatternRewriter &rewriter) const override {
    unsigned index = op.getIndex();
    if (auto construct =
            op.getInput().getDefiningOp<SimAggregateConstructOp>()) {
      rewriter.replaceOp(op, construct.getElements()[index]);
      return success();
    }
    if (op.getInput().getDefiningOp<SimAggregateDefaultOp>()) {
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    if (auto insert = op.getInput().getDefiningOp<SimAggregateInsertOp>()) {
      if (insert.getIndex() == index) {
        rewriter.replaceOp(op, insert.getReplacement());
        return success();
      }
      auto replacement = SimAggregateExtractOp::create(
          rewriter, op.getLoc(), op.getResult().getType(), insert.getInput(),
          op.getIndexAttr());
      rewriter.replaceOp(op, replacement.getResult());
      return success();
    }
    if (auto load = op.getInput().getDefiningOp<SimRefLoadOp>()) {
      Value replacement;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(load);
        Type refType = RefType::get(op.getContext(), op.getResult().getType());
        auto view = SimRefSubelementOp::create(
            rewriter, op.getLoc(), refType, load.getReference(),
            rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(index)}));
        replacement = SimRefLoadOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), view.getResult());
      }
      rewriter.replaceOp(op, replacement);
      return success();
    }
    return failure();
  }
};

struct SimplifyPackedFlatten final : OpRewritePattern<SimPackedFlattenOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimPackedFlattenOp op,
                                PatternRewriter &rewriter) const override {
    auto inverse = op.getInput().getDefiningOp<SimPackedUnflattenOp>();
    if (!inverse || inverse.getInput().getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, inverse.getInput());
    return success();
  }
};

struct SimplifyPackedUnflatten final : OpRewritePattern<SimPackedUnflattenOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimPackedUnflattenOp op,
                                PatternRewriter &rewriter) const override {
    auto inverse = op.getInput().getDefiningOp<SimPackedFlattenOp>();
    if (!inverse || inverse.getInput().getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, inverse.getInput());
    return success();
  }
};

struct SimplifyUnionExtract final : OpRewritePattern<SimUnionExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimUnionExtractOp op,
                                PatternRewriter &rewriter) const override {
    unsigned index = op.getIndex();
    if (auto construct = op.getInput().getDefiningOp<SimUnionConstructOp>();
        construct && construct.getIndex() == index) {
      rewriter.replaceOp(op, construct.getValue());
      return success();
    }
    if (op.getInput().getDefiningOp<SimAggregateDefaultOp>()) {
      Type unionType = op.getInput().getType();
      if (auto packed = dyn_cast<PackedUnionType>(unionType)) {
        if (packed.getIsTagged() &&
            (containsFourStateLeaf(unionType) || index != 0))
          return failure();
      } else if (auto unpacked = dyn_cast<UnpackedUnionType>(unionType)) {
        if (unpacked.getIsTagged() || index != 0)
          return failure();
      }
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    if (auto load = op.getInput().getDefiningOp<SimRefLoadOp>()) {
      Value replacement;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(load);
        Type refType = RefType::get(op.getContext(), op.getResult().getType());
        auto view = SimRefSubelementOp::create(
            rewriter, op.getLoc(), refType, load.getReference(),
            rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(index)}));
        replacement = SimRefLoadOp::create(
            rewriter, op.getLoc(), op.getResult().getType(), view.getResult());
      }
      rewriter.replaceOp(op, replacement);
      return success();
    }
    return failure();
  }
};

struct SimplifyAggregateConstruct final
    : OpRewritePattern<SimAggregateConstructOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateConstructOp op,
                                PatternRewriter &rewriter) const override {
    Value source;
    for (auto [index, element] : llvm::enumerate(op.getElements())) {
      auto extract = element.getDefiningOp<SimAggregateExtractOp>();
      if (!extract || extract.getIndex() != index ||
          (source && source != extract.getInput()))
        return failure();
      source = extract.getInput();
    }
    if (!source || source.getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, source);
    return success();
  }
};

struct SimplifyAggregateInsert final : OpRewritePattern<SimAggregateInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateInsertOp op,
                                PatternRewriter &rewriter) const override {
    if (auto extract =
            op.getReplacement().getDefiningOp<SimAggregateExtractOp>();
        extract && extract.getInput() == op.getInput() &&
        extract.getIndex() == op.getIndex()) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }
    auto nested = op.getInput().getDefiningOp<SimAggregateInsertOp>();
    if (!nested || nested.getIndex() != op.getIndex())
      return failure();
    auto replacement = SimAggregateInsertOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        op.getReplacement(), op.getIndexAttr());
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

struct ConstantArrayExtract final : OpRewritePattern<SimArrayDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    bool unknown;
    std::optional<int64_t> sourceIndex =
        getConstantSourceIndex(op.getIndex(), unknown);
    if (!sourceIndex && !unknown)
      return failure();
    std::optional<unsigned> ordinal =
        sourceIndex
            ? getArrayElementOrdinal(op.getInput().getType(), *sourceIndex)
            : std::nullopt;
    if (!ordinal) {
      Value value = materializeDefaultValue(rewriter, op.getLoc(),
                                            op.getResult().getType());
      if (!value)
        return failure();
      rewriter.replaceOp(op, value);
      return success();
    }
    auto replacement = SimAggregateExtractOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        rewriter.getI64IntegerAttr(*ordinal));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

// Conservatively true unless the operation is known not to write memory. An
// operation with regions or without the effect interface is assumed to write.
static bool mayWriteMemory(Operation *op) {
  if (isMemoryEffectFree(op))
    return false;
  if (op->getNumRegions() != 0)
    return true;
  auto interface = dyn_cast<MemoryEffectOpInterface>(op);
  if (!interface)
    return true;
  return interface.hasEffect<MemoryEffects::Write>() ||
         interface.hasEffect<MemoryEffects::Free>();
}

// Read one element through the reference instead of loading the whole array
// and selecting out of the loaded value, mirroring what SimplifyAggregateExtract
// does for a constant ordinal. Without this a dynamically indexed read of a
// large array becomes a multi-kilobit SSA value and a full-width shift, which
// is quadratic to analyze and needlessly slow to run.
struct DynamicArrayExtractThroughReference final
    : OpRewritePattern<SimArrayDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto load = op.getInput().getDefiningOp<SimRefLoadOp>();
    if (!load)
      return failure();
    // The index is usually computed after the wide load, so the narrow read is
    // built here rather than in the load's place. Sinking a read past other
    // reads is fine; anything that may write could make it observe a newer
    // value than the load did.
    if (load->getBlock() != op->getBlock())
      return failure();
    for (Operation *between = load->getNextNode(); between != op;
         between = between->getNextNode())
      if (!between || mayWriteMemory(between))
        return failure();
    Type refType = RefType::get(op.getContext(), op.getResult().getType());
    auto view = SimRefArrayElementOp::create(
        rewriter, op.getLoc(), refType, load.getReference(), op.getIndex());
    rewriter.replaceOpWithNewOp<SimRefLoadOp>(op, op.getResult().getType(),
                                              view.getResult());
    return success();
  }
};

template <typename DynamicOp, typename StaticOp>
struct ConstantArrayView final : OpRewritePattern<DynamicOp> {
  using OpRewritePattern<DynamicOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(DynamicOp op,
                                PatternRewriter &rewriter) const override {
    bool unknown;
    std::optional<int64_t> sourceIndex =
        getConstantSourceIndex(op.getIndex(), unknown);
    if (!sourceIndex)
      return failure();
    Type arrayType = op.getInput().getType().getElementType();
    std::optional<unsigned> ordinal =
        getArrayElementOrdinal(arrayType, *sourceIndex);
    if (!ordinal)
      return failure();
    auto replacement = StaticOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        rewriter.getDenseI64ArrayAttr({static_cast<int64_t>(*ordinal)}));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

template <typename ViewOp>
struct FlattenSubelementPath final : OpRewritePattern<ViewOp> {
  using OpRewritePattern<ViewOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ViewOp op,
                                PatternRewriter &rewriter) const override {
    auto nested = op.getInput().template getDefiningOp<ViewOp>();
    if (!nested)
      return failure();
    SmallVector<int64_t> indices(nested.getIndices());
    llvm::append_range(indices, op.getIndices());
    auto replacement = ViewOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), nested.getInput(),
        rewriter.getDenseI64ArrayAttr(indices));
    rewriter.replaceOp(op, replacement.getResult());
    return success();
  }
};

} // namespace

void SimPackedFlattenOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<SimplifyPackedFlatten>(context);
}

void SimPackedUnflattenOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyPackedUnflatten>(context);
}

void SimAggregateConstructOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateConstruct>(context);
}

void SimAggregateExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateExtract>(context);
}

void SimAggregateInsertOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateInsert>(context);
}

void SimArrayDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantArrayExtract, DynamicArrayExtractThroughReference>(
      context);
}

void SimUnionConstructOp::getCanonicalizationPatterns(RewritePatternSet &,
                                                      MLIRContext *) {}

void SimUnionExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyUnionExtract>(context);
}

void SimRefSubelementOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<FlattenSubelementPath<SimRefSubelementOp>>(context);
}

void SimRefArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantArrayView<SimRefArrayElementOp, SimRefSubelementOp>>(
      context);
}

void SimDriverSubelementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FlattenSubelementPath<SimDriverSubelementOp>>(context);
}

void SimDriverArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results
      .add<ConstantArrayView<SimDriverArrayElementOp, SimDriverSubelementOp>>(
          context);
}

void SimLogicBinaryOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<NormalizeBinaryConstant>(context);
}

void SimLogicCompareOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<NormalizeCompareConstant>(context);
}

void SimLogicResizeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<CollapseResizeChain>(context);
}

void SimLogicConcatOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<FlattenConcat, ReplicateRepeatedConcatInput,
              MergeAdjacentConcatExtracts>(context);
}

void SimLogicReplicateOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<CombineReplication>(context);
}

void SimLogicExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimLogicExtractOp>,
              SimplifyLogicExtractSource>(context);
}

void SimLogicDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantDynamicExtract<SimLogicDynExtractOp, SimLogicExtractOp>>(
      context);
}

void SimLogicDynInsertOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantDynamicInsert<SimLogicDynInsertOp, SimLogicInsertOp>>(
      context);
}

void SimLogicInsertOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<RemoveOverwrittenInsert>(context);
}

void SimRefExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimRefExtractOp>>(context);
}

void SimNetExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimNetExtractOp>>(context);
}

void SimRefDynExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<ConstantDynamicExtract<SimRefDynExtractOp, SimRefExtractOp>>(
      context);
}

void SimDriverExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimDriverExtractOp>>(context);
}

void SimDriverDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results
      .add<ConstantDynamicExtract<SimDriverDynExtractOp, SimDriverExtractOp>>(
          context);
}

} // namespace obelisk::sim
