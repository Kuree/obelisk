//===- SimulationCanonicalization.cpp - Folding and canonicalization ===//
//
// Constant folding for the logic ops and the rewrite patterns registered as
// canonicalizations for logic, aggregate, and reference view operations.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
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

/// Planes of a logic constant without X or Z bits whose value satisfies
/// `predicate`.
static bool isKnownPlanes(const std::optional<LogicPlanes> &planes,
                          llvm::function_ref<bool(const APInt &)> predicate) {
  return planes && planes->unknown.isZero() && predicate(planes->value);
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
  for (uint64_t replacementBit = 0; replacementBit < replacement.getBitWidth();
       ++replacementBit) {
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

/// A wholly contained or disjoint slice can bypass a fixed integer insertion.
/// Keep partial and out-of-bounds selections on their original operations.
static std::optional<std::pair<Value, uint64_t>>
getBitsInsertSlice(Value input, uint64_t low, uint64_t width) {
  auto insert = input.getDefiningOp<SimBitsDynInsertOp>();
  if (!insert)
    return std::nullopt;
  uint64_t inputWidth = insert.getType().getWidth();
  if (low > inputWidth || width > inputWidth - low)
    return std::nullopt;
  auto index = getConstantIndex(insert.getLowBit());
  uint64_t insertLow;
  uint64_t insertWidth = insert.getReplacement().getType().getWidth();
  if (!index || !index->value ||
      !isKnownInRangeIndex(*index->value, inputWidth, insertWidth, insertLow))
    return std::nullopt;
  uint64_t high = low + width, insertHigh = insertLow + insertWidth;
  if (high <= insertLow || low >= insertHigh)
    return std::make_pair(insert.getInput(), low);
  if (low >= insertLow && high <= insertHigh)
    return std::make_pair(insert.getReplacement(), low - insertLow);
  return std::nullopt;
}

} // namespace

// Identity equality is reflexive even for null handles. Keep queries of
// mutable object state (process status, event triggering, etc.) separate.
OpFoldResult SimChandleEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs())
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimCovergroupEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs() || (getLhs().getDefiningOp<SimCovergroupNullOp>() &&
                               getRhs().getDefiningOp<SimCovergroupNullOp>()))
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimProcessEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs())
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimSemaphoreEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs() || (getLhs().getDefiningOp<SimManagedNullOp>() &&
                               getRhs().getDefiningOp<SimManagedNullOp>()))
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimMailboxEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs() || (getLhs().getDefiningOp<SimManagedNullOp>() &&
                               getRhs().getDefiningOp<SimManagedNullOp>()))
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimEventEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs())
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimVirtualInterfaceEqualOp::fold(FoldAdaptor) {
  if (getLhs() == getRhs())
    return BoolAttr::get(getContext(), true);
  return {};
}
OpFoldResult SimVirtualInterfaceCastOp::fold(FoldAdaptor) {
  if (getInput().getType() == getResult().getType())
    return getInput();
  return {};
}
OpFoldResult SimManagedIsNullOp::fold(FoldAdaptor) {
  if (getInput().getDefiningOp<SimManagedNullOp>())
    return BoolAttr::get(getContext(), true);
  return {};
}

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
  bool selected[4] = {};
  for (Attribute attribute : adaptor.getControls()) {
    auto control = getLogicPlanes(attribute);
    if (!control)
      return {};
    unsigned state =
        (control->unknown[0] ? 2u : 0u) | (control->value[0] ? 1u : 0u);
    selected[state] = true;
  }

  // Selecting every state counts the packed width, independently of its value
  // (IEEE 1800-2023 20.9). Do not use the storage span of unpacked aggregates:
  // their ABI padding is not part of the language bitstream.
  if (llvm::all_of(selected, [](bool state) { return state; })) {
    if (auto width = getPackedWidth(getInput().getType()))
      return IntegerAttr::get(IntegerType::get(getContext(), 32),
                              APInt(32, *width));
  }
  auto input = getLogicPlanes(adaptor.getInput());
  if (auto bits = dyn_cast_or_null<IntegerAttr>(adaptor.getInput()))
    input = LogicPlanes{bits.getValue(),
                        APInt::getZero(bits.getValue().getBitWidth())};
  if (!input)
    return {};

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
  // After X/Z-to-zero conversion every one-bit argument is zero or one.
  if (getInput().getType().getWidth() == 1)
    return IntegerAttr::get(getResult().getType(), 0);
  auto input = getLogicPlanes(adaptor.getInput());
  if (!input)
    return {};
  APInt value = input->value & ~input->unknown;
  uint64_t result = value.isZero() ? 0 : (value - 1).getActiveBits();
  return IntegerAttr::get(IntegerType::get(getContext(), 32),
                          APInt(32, result));
}

OpFoldResult SimLogicIsTrueOp::fold(FoldAdaptor adaptor) {
  if (auto from = getInput().getDefiningOp<SimLogicFromBitsOp>();
      from && from.getInput().getType().isInteger(1))
    return from.getInput();
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
  APInt mismatch = (trueValue->value ^ falseValue->value) | trueValue->unknown |
                   falseValue->unknown;
  LogicPlanes result{trueValue->value & ~mismatch, mismatch};
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
      return emitOpError("logical negation must produce !simulation.logic<1>");
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
    return isKnownPlanes(planes,
                         [](const APInt &bits) { return bits.isZero(); });
  };
  auto isKnownOnes = [](const std::optional<LogicPlanes> &planes) {
    return isKnownPlanes(planes,
                         [](const APInt &bits) { return bits.isAllOnes(); });
  };
  unsigned width = getResult().getType().getWidth();
  if (getKind() == BinaryKind::And && (isKnownZero(lhs) || isKnownZero(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});
  if (getKind() == BinaryKind::Or && (isKnownOnes(lhs) || isKnownOnes(rhs)))
    return getLogicAttribute(getContext(),
                             {APInt::getAllOnes(width), APInt::getZero(width)});
  // Arithmetic propagates any X/Z to the whole result, even multiplication
  // by zero. Division/remainder by zero also produces X (11.4.3).
  switch (getKind()) {
  case BinaryKind::And:
  case BinaryKind::Or:
  case BinaryKind::Xor:
  case BinaryKind::Xnor:
    break;
  default:
    if ((lhs && !lhs->unknown.isZero()) || (rhs && !rhs->unknown.isZero()) ||
        ((getKind() == BinaryKind::UDiv || getKind() == BinaryKind::SDiv ||
          getKind() == BinaryKind::UMod || getKind() == BinaryKind::SMod) &&
         isKnownZero(rhs)))
      return getLogicAttribute(getContext(), getCanonicalUnknown(width));
    break;
  }
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

LogicalResult SimLogicPowerOp::verify() {
  if (getBase().getType() != getResult().getType())
    return emitOpError("base and result types must match");
  return success();
}

OpFoldResult SimLogicPowerOp::fold(FoldAdaptor adaptor) {
  auto base = getLogicPlanes(adaptor.getBase());
  auto exponent = getLogicPlanes(adaptor.getExponent());
  unsigned width = getResult().getType().getWidth();
  if ((base && !base->unknown.isZero()) ||
      (exponent && !exponent->unknown.isZero()))
    return getLogicAttribute(getContext(), getCanonicalUnknown(width));
  if (!base || !exponent)
    return {};

  APInt value(width, 1);
  APInt factor = base->value;
  APInt remaining = exponent->value;
  while (!remaining.isZero()) {
    if (remaining[0])
      value *= factor;
    remaining.lshrInPlace(1);
    if (!remaining.isZero())
      factor *= factor;
  }
  return getLogicAttribute(getContext(),
                           {std::move(value), APInt::getZero(width)});
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
  // IEEE 1800-2023 11.4.10: logical shifts fill every vacated bit with
  // zero, even when the shifted-out bits were X/Z. The amount is unsigned.
  if (amount->value->uge(width) && getKind() != ShiftKind::RightArith)
    return getLogicAttribute(getContext(),
                             {APInt::getZero(width), APInt::getZero(width)});

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
  bool deterministic = integerResult || getKind() == CompareKind::WildEq ||
                       getKind() == CompareKind::WildNe;
  if (deterministic && getLhs() == getRhs()) {
    bool equal =
        getKind() != CompareKind::CaseNe && getKind() != CompareKind::WildNe;
    if (integerResult)
      return IntegerAttr::get(getResult().getType(), equal);
    return getLogicAttribute(getContext(), getLogicBoolean(equal));
  }

  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());
  // IEEE 1800-2023 11.4.6: only the RHS supplies wildcard bits.
  if (rhs && rhs->unknown.isAllOnes() &&
      (getKind() == CompareKind::WildEq || getKind() == CompareKind::WildNe))
    return getLogicAttribute(getContext(),
                             getLogicBoolean(getKind() == CompareKind::WildEq));
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
        "four-state comparisons must produce !simulation.logic<1>");
  if (auto logic = dyn_cast<LogicType>(result); logic && logic.getWidth() != 1)
    return emitOpError("comparison result logic width must be one");
  return success();
}

LogicalResult SimLogicCaseDifferenceMaskOp::verify() {
  auto result = dyn_cast<IntegerType>(getResult().getType());
  if (!result || result.getWidth() != getLhs().getType().getWidth())
    return emitOpError("result width must match the logic operands");
  return success();
}

OpFoldResult SimLogicCaseDifferenceMaskOp::fold(FoldAdaptor adaptor) {
  auto type = cast<IntegerType>(getResult().getType());
  if (getLhs() == getRhs())
    return IntegerAttr::get(type, APInt::getZero(type.getWidth()));
  auto lhs = getLogicPlanes(adaptor.getLhs());
  auto rhs = getLogicPlanes(adaptor.getRhs());
  if (!lhs || !rhs)
    return {};
  return IntegerAttr::get(type, (lhs->value ^ rhs->value) |
                                    (lhs->unknown ^ rhs->unknown));
}

LogicalResult SimDriverDriveInertialPathOp::verify() {
  std::optional<unsigned> width = getPackedWidth(getValue().getType());
  auto sameWidth = [&](Value mask) {
    auto type = dyn_cast<IntegerType>(mask.getType());
    return width && type && type.getWidth() == *width;
  };
  if (!sameWidth(getActiveMask()) || !sameWidth(getRiseMask()) ||
      !sameWidth(getFallMask()) || !sameWidth(getTurnoffMask()))
    return emitOpError("all masks must match the packed driven width");
  if (Value transitions = getPulseTransitionMasks()) {
    auto type = dyn_cast<IntegerType>(transitions.getType());
    if (!width || !type || type.getWidth() != *width * 12)
      return emitOpError(
          "packed pulse transition masks must have twelve times the driven "
          "width");
  }
  if (getGroupCount() == 0 || getGroup() >= getGroupCount())
    return emitOpError("group index must be within a nonempty batch");
  bool defaultReject = getPulseReject() == UINT64_MAX;
  bool defaultError = getPulseError() == UINT64_MAX;
  if (!getPulseTransitionMasks() &&
      (!defaultReject || !defaultError || getPulseOnDetect() ||
       getPulseShowCancelled()))
    return emitOpError(
        "explicit pulse policy requires packed pulse transition masks");
  if (defaultReject != defaultError ||
      (!defaultReject && getPulseError() < getPulseReject()))
    return emitOpError("pulse limits must be defaulted or nonnegative with "
                       "error not less than reject");
  return success();
}

LogicalResult SimRefStoreInertialPathOp::verify() {
  std::optional<unsigned> width = getPackedWidth(getValue().getType());
  auto sameWidth = [&](Value mask) {
    auto type = dyn_cast<IntegerType>(mask.getType());
    return width && type && type.getWidth() == *width;
  };
  if (!sameWidth(getWriteMask()) || !sameWidth(getActiveMask()) ||
      !sameWidth(getRiseMask()) || !sameWidth(getFallMask()) ||
      !sameWidth(getTurnoffMask()))
    return emitOpError("all masks must match the packed stored width");
  if (Value transitions = getPulseTransitionMasks()) {
    auto type = dyn_cast<IntegerType>(transitions.getType());
    if (!width || !type || type.getWidth() != *width * 12)
      return emitOpError(
          "packed pulse transition masks must have twelve times the stored "
          "width");
  }
  if (getGroupCount() == 0 || getGroup() >= getGroupCount())
    return emitOpError("group index must be within a nonempty batch");
  bool defaultReject = getPulseReject() == UINT64_MAX;
  bool defaultError = getPulseError() == UINT64_MAX;
  if (!getPulseTransitionMasks() &&
      (!defaultReject || !defaultError || getPulseOnDetect() ||
       getPulseShowCancelled()))
    return emitOpError(
        "explicit pulse policy requires packed pulse transition masks");
  if (defaultReject != defaultError ||
      (!defaultReject && getPulseError() < getPulseReject()))
    return emitOpError("pulse limits must be defaulted or nonnegative with "
                       "error not less than reject");
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
  // IEEE 1800-2023 11.5.1: a disjoint selection is independent of the
  // input. Use arbitrary-width comparisons before converting to host indices.
  if (index->value->isNegative()
          ? (-*index->value).uge(resultWidth)
          : index->value->uge(getInput().getType().getWidth()))
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
  if (index->value->isNegative()
          ? (-*index->value).uge(resultWidth)
          : index->value->uge(getInput().getType().getWidth()))
    return IntegerAttr::get(getResult().getType(), APInt::getZero(resultWidth));
  if (index->value->isZero() && resultWidth == getInput().getType().getWidth())
    return getInput();
  uint64_t low;
  if (isKnownInRangeIndex(*index->value, getInput().getType().getWidth(),
                          resultWidth, low))
    if (auto source = getBitsInsertSlice(getInput(), low, resultWidth);
        source && source->second == 0 &&
        source->first.getType() == getResult().getType())
      return source->first;
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
  if (index->value->isNegative()
          ? (-*index->value).uge(getReplacement().getType().getWidth())
          : index->value->uge(getInput().getType().getWidth()))
    return getInput();
  if (index->value->isZero() &&
      getInput().getType() == getReplacement().getType())
    return getReplacement();
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
  if (index->value->isNegative()
          ? (-*index->value).uge(getReplacement().getType().getWidth())
          : index->value->uge(getInput().getType().getWidth()))
    return getInput();
  if (index->value->isZero() &&
      getInput().getType() == getReplacement().getType())
    return getReplacement();
  auto input = dyn_cast_or_null<IntegerAttr>(adaptor.getInput());
  auto replacement = dyn_cast_or_null<IntegerAttr>(adaptor.getReplacement());
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
  if (auto net = dyn_cast<NetType>(type))
    return getPackedWidth(net.getElementType());
  return std::nullopt;
}

static std::optional<LogicPlanes> getConstantPlanes(Value value) {
  Attribute attribute;
  if (!matchPattern(value, m_Constant(&attribute)))
    return std::nullopt;
  return getLogicPlanes(attribute);
}

/// A logic constant without X or Z bits whose value satisfies `predicate`.
static bool isKnownConstant(Value value,
                            llvm::function_ref<bool(const APInt &)> predicate) {
  return isKnownPlanes(getConstantPlanes(value), predicate);
}

static bool isKnownZero(Value value) {
  return isKnownConstant(value,
                         [](const APInt &bits) { return bits.isZero(); });
}

static bool isKnownAllOnes(Value value) {
  return isKnownConstant(value,
                         [](const APInt &bits) { return bits.isAllOnes(); });
}

static bool isKnownOne(Value value) {
  return isKnownConstant(value, [](const APInt &bits) { return bits.isOne(); });
}

static Value createLogicConstant(OpBuilder &builder, Location location,
                                 unsigned width, const APInt &value,
                                 const APInt &unknown) {
  auto plane = builder.getIntegerType(width);
  return SimLogicConstantOp::create(builder, location,
                                    LogicType::get(builder.getContext(), width),
                                    builder.getIntegerAttr(plane, value),
                                    builder.getIntegerAttr(plane, unknown));
}

static Value createKnownLogic(OpBuilder &builder, Location location,
                              unsigned width, const APInt &value) {
  return createLogicConstant(builder, location, width, value,
                             APInt::getZero(width));
}

static Value createIntegerConstant(OpBuilder &builder, Location location,
                                   Type type, const APInt &value) {
  return arith::ConstantOp::create(
      builder, location, cast<IntegerType>(type),
      builder.getIntegerAttr(
          type, value.zextOrTrunc(cast<IntegerType>(type).getWidth())));
}

/// IEEE 1800-2023 11.4: every operator that computes bits, rather than
/// moving them, produces only 0, 1 or X. Its result never holds Z, so the
/// Z-to-X normalization of a later bitwise operation is the identity on it.
/// This is a property of the defining operation alone; no operand is
/// inspected recursively.
static bool cannotHoldHighImpedance(Value value) {
  if (std::optional<LogicPlanes> planes = getConstantPlanes(value))
    return (planes->value & planes->unknown).isZero();
  Operation *op = value.getDefiningOp();
  if (!op || !isa<LogicType>(value.getType()))
    return false;
  if (auto unary = dyn_cast<SimLogicUnaryOp>(op))
    return unary.getKind() != UnaryKind::Plus;
  return isa<SimLogicFromBitsOp, SimLogicBinaryOp, SimLogicReductionOp,
             SimLogicLogicalOp, SimLogicPowerOp, SimLogicCompareOp>(op);
}

/// Look through producers that preserve whether a value has a known one bit
/// and whether every bit is a known zero: replication, extension and the OR
/// reduction (11.4.9, 11.4.12.1, 11.6.1).
static Value stripTruthPreservingProducers(Value value) {
  for (;;) {
    if (auto replicate = value.getDefiningOp<SimLogicReplicateOp>())
      value = replicate.getInput();
    else if (auto resize = value.getDefiningOp<SimLogicResizeOp>();
             resize && resize.getType().getWidth() >=
                           resize.getInput().getType().getWidth())
      value = resize.getInput();
    else if (auto reduction = value.getDefiningOp<SimLogicReductionOp>();
             reduction && reduction.getKind() == ReductionKind::Or)
      value = reduction.getInput();
    else
      return value;
  }
}

/// A logical negation, or a one-bit bitwise negation, which canonicalization
/// prefers because it keeps the type: both map 0, 1, X and Z to 1, 0, X and X
/// (11.4.7, Table 11-15). Truth rewrites must accept both spellings.
static bool isTruthNegation(SimLogicUnaryOp op) {
  return op.getKind() == UnaryKind::LogicalNot ||
         (op.getKind() == UnaryKind::BitNot && op.getType().getWidth() == 1);
}

/// The canonical negation of the truth value of `value`.
static SimLogicUnaryOp createNegation(OpBuilder &builder, Location location,
                                      Value value) {
  auto type = cast<LogicType>(value.getType());
  return SimLogicUnaryOp::create(
      builder, location, LogicType::get(builder.getContext(), 1),
      type.getWidth() == 1 ? UnaryKind::BitNot : UnaryKind::LogicalNot, value);
}

/// IEEE 1800-2023 11.4.3: an arithmetic result is either fully known or
/// entirely X. Arithmetic identities hold on such a value even though they
/// do not hold on an arbitrary four-state value.
static bool isWholeValueArithmetic(Value value) {
  Operation *op = value.getDefiningOp();
  if (!op)
    return false;
  if (isa<SimLogicFromBitsOp, SimLogicPowerOp>(op))
    return true;
  if (auto unary = dyn_cast<SimLogicUnaryOp>(op))
    return unary.getKind() == UnaryKind::Negate;
  auto binary = dyn_cast<SimLogicBinaryOp>(op);
  if (!binary)
    return false;
  switch (binary.getKind()) {
  case BinaryKind::And:
  case BinaryKind::Or:
  case BinaryKind::Xor:
  case BinaryKind::Xnor:
    return false;
  default:
    return true;
  }
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
    if (innerWidth == sourceWidth) {
      // The inner resize is an identity, so only the outer extension (or
      // truncation) is observable. Its signedness must not be replaced by
      // the inner one: a sign extension of the same bits stays one.
      signedResult = op.getIsSigned();
    } else if (resultWidth <= std::min(sourceWidth, innerWidth)) {
      // Both resizes only contribute a low-bit truncation.
      signedResult = false;
    } else if (innerWidth > sourceWidth && resultWidth <= innerWidth) {
      // The result observes the extension performed by the inner resize.
      signedResult = inner.getIsSigned();
    } else if (innerWidth > sourceWidth && resultWidth > innerWidth &&
               (!inner.getIsSigned() || op.getIsSigned())) {
      // Repeated zero-extension, or repeated sign-extension, composes. A
      // sign extension of a zero-extended value sees a known-zero sign bit.
      signedResult = inner.getIsSigned();
    } else {
      return failure();
    }

    if (resultWidth == sourceWidth) {
      rewriter.replaceOp(op, inner.getInput());
      return success();
    }
    auto replacement = SimLogicResizeOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), inner.getInput(),
        rewriter.getBoolAttr(signedResult));
    replaceWithNewOp(rewriter, op, replacement);
    return success();
  }
};

/// Keep two-state computations in arith so standard constant propagation,
/// integer range analysis and induction recognition can see their operands.
/// Match only an explicit zero unknown plane; never infer it from a parameter
/// name, a destination type, or the absence of an X/Z literal in the source.
static std::optional<OpFoldResult> matchTwoStateValue(Value value) {
  if (auto from = value.getDefiningOp<SimLogicFromBitsOp>())
    return from.getInput();
  Attribute attribute;
  if (!matchPattern(value, m_Constant(&attribute)))
    return std::nullopt;
  auto planes = getLogicPlanes(attribute);
  if (!planes || !planes->unknown.isZero())
    return std::nullopt;
  return IntegerAttr::get(
      IntegerType::get(value.getContext(), planes->value.getBitWidth()),
      planes->value);
}

static Value materializeBits(OpFoldResult bits, Location loc,
                             PatternRewriter &rewriter) {
  if (auto value = dyn_cast<Value>(bits))
    return value;
  return arith::ConstantOp::create(rewriter, loc,
                                   cast<IntegerAttr>(cast<Attribute>(bits)));
}

struct TwoStateBinary final : OpRewritePattern<SimLogicBinaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicBinaryOp op,
                                PatternRewriter &rewriter) const override {
    // IEEE 1800-2023 11.4.3, 11.4.8 and 11.6: these operations preserve
    // knownness and wrap at the already resolved expression width.
    auto left = matchTwoStateValue(op.getLhs());
    auto right = matchTwoStateValue(op.getRhs());
    if (!left || !right)
      return failure();
    // Division and remainder are two-state only for a known nonzero divisor:
    // a zero divisor produces X (11.4.3). The signed MIN / -1 overflow wraps,
    // so -1 is expressed as negation instead of a poison-producing divsi.
    bool division = false;
    std::optional<APInt> divisor;
    switch (op.getKind()) {
    case BinaryKind::UDiv:
    case BinaryKind::SDiv:
    case BinaryKind::UMod:
    case BinaryKind::SMod:
      division = true;
      if (auto attribute = dyn_cast<Attribute>(*right))
        divisor = cast<IntegerAttr>(attribute).getValue();
      if (!divisor || divisor->isZero())
        return failure();
      break;
    default:
      break;
    }
    Value lhs = materializeBits(*left, op.getLoc(), rewriter);
    Value rhs = materializeBits(*right, op.getLoc(), rewriter);
    Value result;
    bool signedDivision =
        op.getKind() == BinaryKind::SDiv || op.getKind() == BinaryKind::SMod;
    if (division && signedDivision && divisor->isAllOnes()) {
      Value zero =
          createIntegerConstant(rewriter, op.getLoc(), lhs.getType(),
                                APInt::getZero(divisor->getBitWidth()));
      result =
          op.getKind() == BinaryKind::SMod
              ? zero
              : Value(arith::SubIOp::create(rewriter, op.getLoc(), zero, lhs));
      replaceWithNewOp(rewriter, op,
                       SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                  op.getType(), result));
      return success();
    }
    switch (op.getKind()) {
    case BinaryKind::UDiv:
      result = arith::DivUIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::UMod:
      result = arith::RemUIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::SDiv:
      result = arith::DivSIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::SMod:
      result = arith::RemSIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::Add:
      result = arith::AddIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::Sub:
      result = arith::SubIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::Mul:
      result = arith::MulIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::And:
      result = arith::AndIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::Or:
      result = arith::OrIOp::create(rewriter, op.getLoc(), lhs, rhs);
      break;
    case BinaryKind::Xor:
    case BinaryKind::Xnor:
      result = arith::XOrIOp::create(rewriter, op.getLoc(), lhs, rhs);
      if (op.getKind() == BinaryKind::Xnor) {
        auto type = cast<IntegerType>(lhs.getType());
        Value ones = arith::ConstantOp::create(
            rewriter, op.getLoc(),
            IntegerAttr::get(type, APInt::getAllOnes(type.getWidth())));
        result = arith::XOrIOp::create(rewriter, op.getLoc(), result, ones);
      }
      break;
    }
    replaceWithNewOp(rewriter, op,
                     SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                op.getType(), result));
    return success();
  }
};

struct TwoStateCompare final : OpRewritePattern<SimLogicCompareOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCompareOp op,
                                PatternRewriter &rewriter) const override {
    auto left = matchTwoStateValue(op.getLhs());
    auto right = matchTwoStateValue(op.getRhs());
    if (!left || !right)
      return failure();
    // IEEE 1800-2023 11.4.4-6: with no X/Z operands all equality variants
    // compare the same bits. Relational signedness remains explicit.
    arith::CmpIPredicate predicate;
    switch (op.getKind()) {
    case CompareKind::Eq:
    case CompareKind::CaseEq:
    case CompareKind::CaseZEq:
    case CompareKind::CaseXZEq:
    case CompareKind::WildEq:
      predicate = arith::CmpIPredicate::eq;
      break;
    case CompareKind::Ne:
    case CompareKind::CaseNe:
    case CompareKind::WildNe:
      predicate = arith::CmpIPredicate::ne;
      break;
    case CompareKind::ULT:
      predicate = arith::CmpIPredicate::ult;
      break;
    case CompareKind::ULE:
      predicate = arith::CmpIPredicate::ule;
      break;
    case CompareKind::UGT:
      predicate = arith::CmpIPredicate::ugt;
      break;
    case CompareKind::UGE:
      predicate = arith::CmpIPredicate::uge;
      break;
    case CompareKind::SLT:
      predicate = arith::CmpIPredicate::slt;
      break;
    case CompareKind::SLE:
      predicate = arith::CmpIPredicate::sle;
      break;
    case CompareKind::SGT:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case CompareKind::SGE:
      predicate = arith::CmpIPredicate::sge;
      break;
    default:
      llvm_unreachable("unknown logic comparison kind");
    }
    Value lhs = materializeBits(*left, op.getLoc(), rewriter);
    Value rhs = materializeBits(*right, op.getLoc(), rewriter);
    auto compare =
        arith::CmpIOp::create(rewriter, op.getLoc(), predicate, lhs, rhs);
    if (isa<IntegerType>(op.getType()))
      replaceWithNewOp(rewriter, op, compare);
    else
      replaceWithNewOp(rewriter, op,
                       SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                  op.getType(), compare));
    return success();
  }
};

struct TwoStateResize final : OpRewritePattern<SimLogicResizeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicResizeOp op,
                                PatternRewriter &rewriter) const override {
    auto from = op.getInput().getDefiningOp<SimLogicFromBitsOp>();
    if (!from)
      return failure();
    auto type = rewriter.getIntegerType(op.getType().getWidth());
    Value bits = from.getInput();
    unsigned width = from.getInput().getType().getWidth();
    if (type.getWidth() < width)
      bits = arith::TruncIOp::create(rewriter, op.getLoc(), type, bits);
    else if (type.getWidth() > width)
      bits = op.getIsSigned()
                 ? arith::ExtSIOp::create(rewriter, op.getLoc(), type, bits)
                       .getResult()
                 : arith::ExtUIOp::create(rewriter, op.getLoc(), type, bits)
                       .getResult();
    replaceWithNewOp(
        rewriter, op,
        SimLogicFromBitsOp::create(rewriter, op.getLoc(), op.getType(), bits));
    return success();
  }
};

struct TwoStateTruth final : OpRewritePattern<SimLogicIsTrueOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicIsTrueOp op,
                                PatternRewriter &rewriter) const override {
    auto from = op.getInput().getDefiningOp<SimLogicFromBitsOp>();
    if (!from)
      return failure();
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(),
        rewriter.getIntegerAttr(from.getInput().getType(), 0));
    replaceWithNewOp(rewriter, op,
                     arith::CmpIOp::create(rewriter, op.getLoc(),
                                           arith::CmpIPredicate::ne,
                                           from.getInput(), zero));
    return success();
  }
};

// The truth-source and resized-conversion rules are registered on their root
// operations. They preserve the resolved expression widths and four-state
// conversion contracts; they do not recursively infer facts about an
// arbitrary defining expression.
struct SimplifyTruthSource final : OpRewritePattern<SimLogicIsTrueOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SimLogicIsTrueOp op,
                                PatternRewriter &rewriter) const override {
    // Truncation can discard the only known one, so it is not stripped.
    Value input = stripTruthPreservingProducers(op.getInput());
    if (input == op.getInput())
      return failure();
    replaceWithNewOp(rewriter, op,
                     SimLogicIsTrueOp::create(rewriter, op.getLoc(), input));
    return success();
  }
};

struct ConvertResizedLogicToBits final : OpRewritePattern<SimLogicToBitsOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SimLogicToBitsOp op,
                                PatternRewriter &rewriter) const override {
    auto resize = op.getInput().getDefiningOp<SimLogicResizeOp>();
    if (!resize || !op.getType().isSignless())
      return failure();
    unsigned inputWidth = resize.getInput().getType().getWidth();
    unsigned outputWidth = op.getType().getWidth();
    Value bits = SimLogicToBitsOp::create(rewriter, op.getLoc(),
                                          rewriter.getIntegerType(inputWidth),
                                          resize.getInput());
    // Clearing an unknown sign bit before sign extension is equivalent to
    // extending both planes and then clearing all unknown bits (LRM 6.11,
    // 11.6, 11.8). This is not a lossless-cast assumption.
    if (outputWidth < inputWidth)
      bits = arith::TruncIOp::create(rewriter, op.getLoc(), op.getType(), bits);
    else if (outputWidth > inputWidth)
      bits = resize.getIsSigned()
                 ? arith::ExtSIOp::create(rewriter, op.getLoc(), op.getType(),
                                          bits)
                       .getResult()
                 : arith::ExtUIOp::create(rewriter, op.getLoc(), op.getType(),
                                          bits)
                       .getResult();
    rewriter.replaceOp(op, bits);
    return success();
  }
};

struct DeduplicateCountControls final : OpRewritePattern<SimLogicCountBitsOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SimLogicCountBitsOp op,
                                PatternRewriter &rewriter) const override {
    llvm::SmallDenseSet<Value, 4> seen;
    SmallVector<Value> controls;
    for (Value control : op.getControls())
      if (seen.insert(control).second)
        controls.push_back(control);
    if (controls.size() == op.getControls().size())
      return failure();
    // Controls select a set of symbols, including dynamic X/Z controls.
    rewriter.modifyOpInPlace(op,
                             [&] { op.getControlsMutable().assign(controls); });
    return success();
  }
};

struct TwoStateUnary final : OpRewritePattern<SimLogicUnaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicUnaryOp op,
                                PatternRewriter &rewriter) const override {
    auto input = matchTwoStateValue(op.getInput());
    if (!input)
      return failure();
    Value bits = materializeBits(*input, op.getLoc(), rewriter);
    auto type = cast<IntegerType>(bits.getType());
    auto constant = [&](APInt value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(),
                                       IntegerAttr::get(type, value));
    };
    Value result;
    switch (op.getKind()) {
    case UnaryKind::Plus:
      rewriter.replaceOp(op, op.getInput());
      return success();
    case UnaryKind::Negate:
      result = arith::SubIOp::create(rewriter, op.getLoc(),
                                     constant(APInt::getZero(type.getWidth())),
                                     bits);
      break;
    case UnaryKind::BitNot:
      result =
          arith::XOrIOp::create(rewriter, op.getLoc(), bits,
                                constant(APInt::getAllOnes(type.getWidth())));
      break;
    case UnaryKind::LogicalNot:
      result = arith::CmpIOp::create(rewriter, op.getLoc(),
                                     arith::CmpIPredicate::eq, bits,
                                     constant(APInt::getZero(type.getWidth())));
      break;
    }
    replaceWithNewOp(rewriter, op,
                     SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                op.getType(), result));
    return success();
  }
};

struct TwoStateReduction final : OpRewritePattern<SimLogicReductionOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReductionOp op,
                                PatternRewriter &rewriter) const override {
    bool allOnes;
    arith::CmpIPredicate predicate;
    switch (op.getKind()) {
    case ReductionKind::And:
    case ReductionKind::Nand:
      allOnes = true;
      predicate = op.getKind() == ReductionKind::And ? arith::CmpIPredicate::eq
                                                     : arith::CmpIPredicate::ne;
      break;
    case ReductionKind::Or:
    case ReductionKind::Nor:
      allOnes = false;
      predicate = op.getKind() == ReductionKind::Or ? arith::CmpIPredicate::ne
                                                    : arith::CmpIPredicate::eq;
      break;
    default:
      return failure();
    }
    auto input = matchTwoStateValue(op.getInput());
    if (!input)
      return failure();
    Value bits = materializeBits(*input, op.getLoc(), rewriter);
    auto type = cast<IntegerType>(bits.getType());
    Value constant = arith::ConstantOp::create(
        rewriter, op.getLoc(),
        IntegerAttr::get(type, allOnes ? APInt::getAllOnes(type.getWidth())
                                       : APInt::getZero(type.getWidth())));
    Value result =
        arith::CmpIOp::create(rewriter, op.getLoc(), predicate, bits, constant);
    replaceWithNewOp(rewriter, op,
                     SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                op.getType(), result));
    return success();
  }
};

/// Reduction of a repeated word is unchanged for idempotent reductions or
/// an odd number of parity terms. Even parity is NOT zero for X/Z inputs.
struct ReduceReplication final : OpRewritePattern<SimLogicReductionOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReductionOp op,
                                PatternRewriter &rewriter) const override {
    auto replicate = op.getInput().getDefiningOp<SimLogicReplicateOp>();
    if (!replicate || ((op.getKind() == ReductionKind::Xor ||
                        op.getKind() == ReductionKind::Xnor) &&
                       !(replicate.getCount() & 1)))
      return failure();
    rewriter.modifyOpInPlace(
        op, [&] { op.getInputMutable().assign(replicate.getInput()); });
    return success();
  }
};

struct SimplifyLogical final : OpRewritePattern<SimLogicLogicalOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicLogicalOp op,
                                PatternRewriter &rewriter) const override {
    Value input;
    if (op.getLhs() == op.getRhs()) {
      input = op.getLhs();
    } else {
      for (unsigned i : {0u, 1u}) {
        Attribute attribute;
        if (!matchPattern(op->getOperand(i), m_Constant(&attribute)))
          continue;
        auto planes = getLogicPlanes(attribute);
        if (!planes)
          continue;
        TruthState truth = getTruth(*planes);
        if (!truth.unknown && truth.value == (op.getKind() == LogicalKind::And))
          input = op->getOperand(1 - i);
      }
    }
    if (input) {
      // A logical identity still normalizes Z to X and reduces wide values.
      replaceWithNewOp(rewriter, op,
                       SimLogicReductionOp::create(rewriter, op.getLoc(),
                                                   op.getType(),
                                                   ReductionKind::Or, input));
      return success();
    }
    auto lhs = matchTwoStateValue(op.getLhs());
    auto rhs = matchTwoStateValue(op.getRhs());
    if (!lhs || !rhs)
      return failure();
    auto truth = [&](OpFoldResult input) -> Value {
      Value bits = materializeBits(input, op.getLoc(), rewriter);
      Value zero = arith::ConstantOp::create(
          rewriter, op.getLoc(), rewriter.getIntegerAttr(bits.getType(), 0));
      return arith::CmpIOp::create(rewriter, op.getLoc(),
                                   arith::CmpIPredicate::ne, bits, zero);
    };
    Value left = truth(*lhs), right = truth(*rhs);
    Value result =
        op.getKind() == LogicalKind::And
            ? Value(arith::AndIOp::create(rewriter, op.getLoc(), left, right))
            : Value(arith::OrIOp::create(rewriter, op.getLoc(), left, right));
    replaceWithNewOp(rewriter, op,
                     SimLogicFromBitsOp::create(rewriter, op.getLoc(),
                                                op.getType(), result));
    return success();
  }
};

struct TwoStateMux final : OpRewritePattern<SimLogicMuxOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicMuxOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getTrueValue() == op.getFalseValue() &&
        matchTwoStateValue(op.getTrueValue())) {
      rewriter.replaceOp(op, op.getTrueValue());
      return success();
    }
    auto condition = matchTwoStateValue(op.getCondition());
    if (!condition)
      return failure();
    Value bits = materializeBits(*condition, op.getLoc(), rewriter);
    replaceWithNewOp(rewriter, op,
                     arith::SelectOp::create(rewriter, op.getLoc(), bits,
                                             op.getTrueValue(),
                                             op.getFalseValue()));
    return success();
  }
};

struct TwoStateDifferenceMask final
    : OpRewritePattern<SimLogicCaseDifferenceMaskOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCaseDifferenceMaskOp op,
                                PatternRewriter &rewriter) const override {
    if (!cast<IntegerType>(op.getType()).isSignless())
      return failure();
    auto lhs = matchTwoStateValue(op.getLhs());
    auto rhs = matchTwoStateValue(op.getRhs());
    if (!lhs || !rhs)
      return failure();
    Value left = materializeBits(*lhs, op.getLoc(), rewriter);
    Value right = materializeBits(*rhs, op.getLoc(), rewriter);
    replaceWithNewOp(rewriter, op,
                     arith::XOrIOp::create(rewriter, op.getLoc(), left, right));
    return success();
  }
};

/// Selection ops accept integer or logic indices. Remove an explicit
/// from_bits without narrowing or changing the declared-range interpretation.
template <typename Op, unsigned IndexOperand>
struct TwoStateIndex final : OpRewritePattern<Op> {
  using OpRewritePattern<Op>::OpRewritePattern;

  LogicalResult matchAndRewrite(Op op,
                                PatternRewriter &rewriter) const override {
    auto from = op->getOperand(IndexOperand)
                    .template getDefiningOp<SimLogicFromBitsOp>();
    if (!from)
      return failure();
    rewriter.modifyOpInPlace(
        op, [&] { op->setOperand(IndexOperand, from.getInput()); });
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

/// Reassociate fixed-width associative operations to expose adjacent constants.
/// Keep the remaining logic operation: x * 0 and x & all_ones are not general
/// four-state identities. The one-use restriction avoids duplicating work.
struct CombineBinaryConstants final : OpRewritePattern<SimLogicBinaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicBinaryOp op,
                                PatternRewriter &rewriter) const override {
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
    auto inner = op.getLhs().getDefiningOp<SimLogicBinaryOp>();
    if (!inner || inner.getKind() != op.getKind() || !inner->hasOneUse() ||
        !isConstantValue(inner.getRhs()) || !isConstantValue(op.getRhs()))
      return failure();
    Value constant = rewriter.createOrFold<SimLogicBinaryOp>(
        op.getLoc(), op.getType(), op.getKind(), inner.getRhs(), op.getRhs());
    replaceWithNewOp(rewriter, op,
                     SimLogicBinaryOp::create(rewriter, op.getLoc(),
                                              op.getType(), op.getKind(),
                                              inner.getLhs(), constant));
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
    // Only a view that selects the whole input *and* keeps its type is the
    // identity. A full-width extract can still retype what it names -- an
    // IEEE 1800-2023 11.5.1 part-select spanning all of an `int` produces
    // `bit [31:0]` -- and replacing that with the input would hand every user
    // a reference whose element type no longer matches the value it carries.
    if (op.getLowBit() == 0 && *inputWidth == *resultWidth &&
        op.getResult().getType() == op.getInput().getType()) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }

    // Only packed bit offsets compose. A net view of an unpacked array element
    // is addressed by its storage offset and must name exactly one element,
    // so a packed slice of that element is not a view of the array.
    auto nested = op.getInput().template getDefiningOp<ExtractOp>();
    if (!nested || !getSelectionWidth(nested.getInput().getType()))
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
    // IEEE 1800-2023 11.4.12 and 11.5.1: a slice may span several
    // concatenation operands. Preserve their bit order and both X/Z planes.
    SmallVector<Value> pieces;
    uint64_t inputLow = 0;
    for (Value input : llvm::reverse(concat.getInputs())) {
      uint64_t inputWidth = cast<LogicType>(input.getType()).getWidth();
      uint64_t inputHigh = inputLow + inputWidth;
      uint64_t overlapLow = std::max(low, inputLow);
      uint64_t overlapHigh = std::min(high, inputHigh);
      if (overlapLow < overlapHigh) {
        if (overlapLow == inputLow && overlapHigh == inputHigh)
          pieces.push_back(input);
        else
          pieces.push_back(SimLogicExtractOp::create(
              rewriter, op.getLoc(),
              LogicType::get(op.getContext(), overlapHigh - overlapLow), input,
              rewriter.getI64IntegerAttr(overlapLow - inputLow)));
      }
      inputLow = inputHigh;
    }
    if (pieces.size() == 1)
      rewriter.replaceOp(op, pieces.front());
    else {
      std::reverse(pieces.begin(), pieces.end());
      auto replacement = SimLogicConcatOp::create(
          rewriter, op.getLoc(), op.getResult().getType(), pieces);
      replaceWithNewOp(rewriter, op, replacement);
    }
    return success();
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
    if (!isKnownInRangeIndex(*index->value, op.getInput().getType().getWidth(),
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

// An inlined automatic local can be allocated on a loop backedge. Mem2reg's
// allocation definition alone does not reset it on each entry (LRM 6.21).
// Make that initialization explicit before exposing its value selections.
struct ExplicitReenteredLocalInitialization final
    : OpRewritePattern<SimRefAllocOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SimRefAllocOp op,
                                PatternRewriter &rewriter) const override {
    if (!getProvenanceSpan(op.getResult().getType().getElementType()) ||
        !op.getPromotableSlots().empty())
      return failure();
    Block *block = op->getBlock();
    if (!llvm::any_of(block->getPredecessors(), [&](Block *predecessor) {
          return block->isReachable(predecessor);
        }))
      return failure();
    rewriter.setInsertionPointAfter(op);
    SimRefStoreOp::create(rewriter, op.getLoc(), op.getInitialValue(),
                          op.getResult());
    return success();
  }
};

/// Offset of a static member path inside packed storage. Tagged member
/// access must retain its active-member check (LRM 7.3.2).
static std::optional<uint64_t> getPackedMemberOffset(Type type,
                                                     ArrayRef<int64_t> path) {
  uint64_t low = 0;
  for (int64_t index : path) {
    if (auto packed = dyn_cast<PackedUnionType>(type);
        packed && packed.getIsTagged())
      return std::nullopt;
    if (index < 0 || uint64_t(index) > UINT_MAX)
      return std::nullopt;
    auto child = getAggregateProvenanceSubelement(type, index);
    if (!child || child->first > UINT64_MAX - low)
      return std::nullopt;
    low += child->first;
    type = getAggregateElementType(type, index);
  }
  return low;
}

/// The four-state view type of two-state packed storage. Packed arrays keep
/// their shape so that dynamic element selections still apply; any other
/// packed type is viewed as its flat bits.
static Type getFourStateViewType(Type type) {
  if (auto array = dyn_cast<PackedArrayType>(type))
    return PackedArrayType::get(type.getContext(),
                                getFourStateViewType(array.getElementType()),
                                array.getLeft(), array.getRight());
  return LogicType::get(type.getContext(), *getPackedWidth(type));
}

/// Rewrite the loads, stores and packed views through a two-state reference
/// as accesses through the four-state view of the same bits. Any other user
/// keeps the two-state reference. Returns whether an access moved.
static bool convertTwoStateAccesses(PatternRewriter &rewriter, Value twoState,
                                    Value fourState) {
  MLIRContext *context = rewriter.getContext();
  Type viewed = cast<RefType>(fourState.getType()).getElementType();
  bool converted = false;
  for (Operation *user : llvm::make_early_inc_range(twoState.getUsers())) {
    rewriter.setInsertionPoint(user);
    Location location = user->getLoc();
    if (auto load = dyn_cast<SimRefLoadOp>(user)) {
      Type type = load.getResult().getType();
      Type scalar = getPackedScalarType(type);
      Value bits = SimRefLoadOp::create(rewriter, location, viewed, fourState);
      bits.getDefiningOp()->setAttrs(load->getAttrs());
      Type logic = LogicType::get(context, *getPackedWidth(type));
      if (viewed != logic)
        bits = SimPackedFlattenOp::create(rewriter, location, logic, bits);
      Value value = SimLogicToBitsOp::create(rewriter, location, scalar, bits);
      if (type != scalar)
        value = SimPackedUnflattenOp::create(rewriter, location, type, value);
      rewriter.replaceOp(load, value);
      converted = true;
      continue;
    }
    if (auto store = dyn_cast<SimRefStoreOp>(user)) {
      if (store.getReference() != twoState)
        continue;
      Value value = store.getValue();
      Type scalar = getPackedScalarType(value.getType());
      if (value.getType() != scalar)
        value = SimPackedFlattenOp::create(rewriter, location, scalar, value);
      value = SimLogicFromBitsOp::create(
          rewriter, location, LogicType::get(context, *getPackedWidth(scalar)),
          value);
      if (viewed != value.getType())
        value = SimPackedUnflattenOp::create(rewriter, location, viewed, value);
      auto replacement =
          SimRefStoreOp::create(rewriter, location, value, fourState);
      replacement->setAttrs(store->getAttrs());
      rewriter.eraseOp(store);
      converted = true;
      continue;
    }
    if (!isa<SimRefExtractOp, SimRefDynExtractOp, SimRefSubelementOp,
             SimRefArrayElementOp>(user) ||
        user->getOperand(0) != twoState)
      continue;
    std::optional<uint64_t> memberOffset;
    if (auto member = dyn_cast<SimRefSubelementOp>(user)) {
      memberOffset = getPackedMemberOffset(
          member.getInput().getType().getElementType(), member.getIndices());
      if (!memberOffset)
        continue;
    }
    if (isa<SimRefArrayElementOp>(user) && !isa<PackedArrayType>(viewed))
      continue;
    Type element = cast<RefType>(user->getResult(0).getType()).getElementType();
    Type viewType = RefType::get(context, getFourStateViewType(element));
    Value view;
    if (auto extract = dyn_cast<SimRefExtractOp>(user))
      view = SimRefExtractOp::create(rewriter, location, viewType, fourState,
                                     extract.getLowBitAttr());
    else if (auto extract = dyn_cast<SimRefDynExtractOp>(user))
      view = SimRefDynExtractOp::create(rewriter, location, viewType, fourState,
                                        extract.getLowBit());
    else if (auto element = dyn_cast<SimRefArrayElementOp>(user))
      view = SimRefArrayElementOp::create(rewriter, location, viewType,
                                          fourState, element.getIndex());
    else
      view = SimRefExtractOp::create(rewriter, location, viewType, fourState,
                                     *memberOffset);
    if (convertTwoStateAccesses(rewriter, user->getResult(0), view))
      converted = true;
    if (view.use_empty())
      rewriter.eraseOp(view.getDefiningOp());
    if (user->use_empty())
      rewriter.eraseOp(user);
  }
  return converted;
}

struct PackedLocalSubelement final : OpRewritePattern<SimRefSubelementOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimRefSubelementOp op,
                                PatternRewriter &rewriter) const override {
    // LRM 6.21, 7.3.1, 11.5.1: an enclosing packed slice can still belong
    // to the same private automatic cell. Expose its nested member selection
    // so the adjacent range-preserving view fold can compose the offsets.
    Value root = op.getInput();
    for (;;) {
      if (auto view = root.getDefiningOp<SimRefExtractOp>())
        root = view.getInput();
      else
        break;
    }
    auto allocation = root.getDefiningOp<SimRefAllocOp>();
    if (!allocation || allocation.getPromotableSlots().empty())
      return failure();
    if (getProvenanceSpan(allocation.getInitialValue().getType()) > 64)
      return failure();
    Type type = op.getInput().getType().getElementType();
    if (!getPackedWidth(type))
      return failure();
    std::optional<uint64_t> low = getPackedMemberOffset(type, op.getIndices());
    if (!low)
      return failure();
    Type parentScalar = getPackedScalarType(type);
    Type memberScalar =
        getPackedScalarType(op.getResult().getType().getElementType());
    if (!parentScalar || !memberScalar)
      return failure();
    // A member whose state domain differs is FourStatePackedMember's.
    if (isa<LogicType>(parentScalar) != isa<LogicType>(memberScalar))
      return failure();
    rewriter.replaceOpWithNewOp<SimRefExtractOp>(op, op.getResult().getType(),
                                                 op.getInput(), *low);
    return success();
  }
};

// LRM 7.2.1, 7.3.1: a two-state member of a four-state packed aggregate
// occupies four-state bits. Reading it converts X and Z to zero; writing it
// stores known bits. A reference view cannot change the state domain, so view
// the four-state bits and convert at every access instead, whatever storage
// the aggregate lives in.
struct FourStatePackedMember final : OpRewritePattern<SimRefSubelementOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimRefSubelementOp op,
                                PatternRewriter &rewriter) const override {
    Type type = op.getInput().getType().getElementType();
    Type member = op.getResult().getType().getElementType();
    Type parentScalar = getPackedScalarType(type);
    Type memberScalar = getPackedScalarType(member);
    if (!parentScalar || !memberScalar || !isa<LogicType>(parentScalar) ||
        isa<LogicType>(memberScalar))
      return failure();
    std::optional<uint64_t> low = getPackedMemberOffset(type, op.getIndices());
    if (!low)
      return failure();
    rewriter.setInsertionPoint(op);
    auto view = SimRefExtractOp::create(
        rewriter, op.getLoc(),
        RefType::get(op.getContext(), getFourStateViewType(member)),
        op.getInput(), *low);
    if (!convertTwoStateAccesses(rewriter, op.getResult(), view)) {
      rewriter.eraseOp(view);
      return failure();
    }
    if (op->use_empty())
      rewriter.eraseOp(op);
    return success();
  }
};

// IEEE 1800-2023 6.21, 7.4.5, 11.5.1: a nonescaping automatic array can
// use whole-value SSA updates. Preserve declared-range index validity and
// rebuild enclosing selections so aliases and overlapping writes still agree.
template <typename View>
struct PromoteLocalArrayView final : OpRewritePattern<View> {
  using OpRewritePattern<View>::OpRewritePattern;

  LogicalResult matchAndRewrite(View op,
                                PatternRewriter &rewriter) const override {
    auto allocation = op.getInput().template getDefiningOp<SimRefAllocOp>();
    if (!allocation || allocation.getPromotableSlots().empty())
      return failure();
    Type rootType = allocation.getResult().getType().getElementType();
    // Whole-value promotion is cheap for a word with static selections.
    // Dynamic unpacked elements should retain native element storage: turning
    // an array of small elements into an integer recreates bit packing on
    // every indexed update, even when the complete array fits in a word.
    auto width = getProvenanceSpan(rootType);
    if (!width)
      return failure();
    bool needsClipping = false;
    bool dynamicUnpackedElement = false;
    SmallVector<Value> pending{allocation.getResult()};
    llvm::SmallDenseSet<Value, 8> visited;
    while (!pending.empty()) {
      Value reference = pending.pop_back_val();
      if (!visited.insert(reference).second)
        continue;
      for (Operation *user : reference.getUsers()) {
        if (user->getParentOp() != allocation->getParentOp())
          return failure();
        if (isa<SimRefArrayElementOp, SimRefSubelementOp, SimRefExtractOp,
                SimRefDynExtractOp>(user)) {
          needsClipping |= isa<SimRefDynExtractOp>(user);
          if (auto element = dyn_cast<SimRefArrayElementOp>(user))
            dynamicUnpackedElement |=
                isa<UnpackedArrayType>(
                    element.getInput().getType().getElementType()) &&
                !matchPattern(element.getIndex(), m_Constant());
          if (auto field = dyn_cast<SimRefSubelementOp>(user)) {
            Type type = field.getInput().getType().getElementType();
            for (int64_t index : field.getIndices()) {
              Type child = getAggregateElementType(type, index);
              // Hard packed union members cover the same bits (LRM 7.3.1).
              // Other union layouts need their own preservation rules.
              if (isa<UnpackedUnionType>(type))
                return failure();
              if (auto packed = dyn_cast<PackedUnionType>(type);
                  packed && (packed.getIsTagged() ||
                             getPackedWidth(type) != getPackedWidth(child)))
                return failure();
              type = child;
            }
          }
          if (isa<SimRefExtractOp, SimRefDynExtractOp>(user)) {
            Type parent =
                cast<RefType>(user->getOperand(0).getType()).getElementType();
            Type child =
                cast<RefType>(user->getResult(0).getType()).getElementType();
            Type parentScalar = getPackedScalarType(parent);
            Type childScalar = getPackedScalarType(child);
            if (!parentScalar || !childScalar ||
                (!isa<LogicType>(parentScalar) && isa<LogicType>(childScalar)))
              return failure();
          }
          pending.push_back(user->getResult(0));
          continue;
        }
        if (auto load = dyn_cast<SimRefLoadOp>(user);
            load && load.getReference() == reference)
          continue;
        if (auto store = dyn_cast<SimRefStoreOp>(user);
            store && store.getReference() == reference)
          continue;
        return failure();
      }
    }
    if ((*width > 64 || dynamicUnpackedElement) && !needsClipping)
      return failure();
    SmallVector<Operation *> accesses;
    SmallVector<Operation *> selectedViews;
    // All views must become value selections together: keeping constant views
    // beside whole-array updates prevents alias-safe promotion (LRM 7.4.5).
    pending.assign(1, allocation.getResult());
    visited.clear();
    while (!pending.empty()) {
      Value reference = pending.pop_back_val();
      if (!visited.insert(reference).second)
        continue;
      for (Operation *user : reference.getUsers())
        if (isa<SimRefArrayElementOp, SimRefSubelementOp, SimRefExtractOp,
                SimRefDynExtractOp>(user)) {
          pending.push_back(user->getResult(0));
          selectedViews.push_back(user);
        } else
          accesses.push_back(user);
    }
    if (accesses.empty())
      return failure();
    for (Operation *access : accesses) {
      Value reference = access->getOperand(isa<SimRefStoreOp>(access) ? 1 : 0);
      SmallVector<Operation *> path;
      while (reference != allocation.getResult()) {
        Operation *view = reference.getDefiningOp();
        path.push_back(view);
        reference = view->getOperand(0);
      }
      if (path.empty())
        continue;
      std::reverse(path.begin(), path.end());
      rewriter.setInsertionPoint(access);
      Value selected = SimRefLoadOp::create(rewriter, access->getLoc(),
                                            rootType, allocation.getResult());
      struct Selection {
        Value parent;
        Value index;
        int64_t ordinal = -1;
        bool unionMember = false;
        Type packedChild;
      };
      SmallVector<Selection> selections;
      for (Operation *view : path) {
        if (auto array = dyn_cast<SimRefArrayElementOp>(view)) {
          selections.push_back({selected, array.getIndex()});
          selected = SimArrayDynExtractOp::create(
              rewriter, access->getLoc(),
              array.getResult().getType().getElementType(), selected,
              array.getIndex());
        } else if (isa<SimRefExtractOp, SimRefDynExtractOp>(view)) {
          Type parentType = selected.getType();
          Type parentScalar = getPackedScalarType(parentType);
          Type child =
              cast<RefType>(view->getResult(0).getType()).getElementType();
          Type childScalar = getPackedScalarType(child);
          Type planeChild = childScalar;
          if (isa<LogicType>(parentScalar) && isa<IntegerType>(childScalar))
            planeChild =
                LogicType::get(op.getContext(), *getPackedWidth(child));
          Value low;
          if (auto fixed = dyn_cast<SimRefExtractOp>(view))
            low = arith::ConstantIntOp::create(rewriter, access->getLoc(),
                                               fixed.getLowBit(), 64);
          else
            low = cast<SimRefDynExtractOp>(view).getLowBit();
          selections.push_back({selected, low, -1, false, child});
          Value bits = selected;
          if (parentType != parentScalar)
            bits = SimPackedFlattenOp::create(rewriter, access->getLoc(),
                                              parentScalar, bits);
          if (isa<LogicType>(parentScalar))
            selected = SimLogicDynExtractOp::create(rewriter, access->getLoc(),
                                                    planeChild, bits, low);
          else
            selected = SimBitsDynExtractOp::create(rewriter, access->getLoc(),
                                                   planeChild, bits, low);
          if (planeChild != childScalar)
            selected = SimLogicToBitsOp::create(rewriter, access->getLoc(),
                                                childScalar, selected);
          if (child != childScalar)
            selected = SimPackedUnflattenOp::create(rewriter, access->getLoc(),
                                                    child, selected);
        } else {
          auto field = cast<SimRefSubelementOp>(view);
          for (int64_t index : field.getIndices()) {
            bool unionMember = isa<PackedUnionType>(selected.getType());
            selections.push_back({selected, {}, index, unionMember});
            selected =
                unionMember
                    ? SimUnionExtractOp::create(
                          rewriter, access->getLoc(),
                          getAggregateElementType(selected.getType(), index),
                          selected, index)
                          .getResult()
                    : SimAggregateExtractOp::create(
                          rewriter, access->getLoc(),
                          getAggregateElementType(selected.getType(), index),
                          selected, index)
                          .getResult();
          }
        }
      }
      if (auto load = dyn_cast<SimRefLoadOp>(access)) {
        rewriter.replaceOp(load, selected);
        continue;
      }
      auto store = cast<SimRefStoreOp>(access);
      selected = store.getValue();
      for (const Selection &selection : llvm::reverse(selections))
        if (selection.packedChild) {
          Type parentType = selection.parent.getType();
          Type parentScalar = getPackedScalarType(parentType);
          Type childScalar = getPackedScalarType(selection.packedChild);
          Value parent = selection.parent;
          if (parentType != parentScalar)
            parent = SimPackedFlattenOp::create(rewriter, store.getLoc(),
                                                parentScalar, parent);
          if (selection.packedChild != childScalar)
            selected = SimPackedFlattenOp::create(rewriter, store.getLoc(),
                                                  childScalar, selected);
          if (isa<LogicType>(parentScalar) && isa<IntegerType>(childScalar))
            selected = SimLogicFromBitsOp::create(
                rewriter, store.getLoc(),
                LogicType::get(op.getContext(),
                               *getPackedWidth(selection.packedChild)),
                selected);
          if (isa<LogicType>(parentScalar))
            selected = SimLogicDynInsertOp::create(rewriter, store.getLoc(),
                                                   parentScalar, parent,
                                                   selected, selection.index);
          else
            selected = SimBitsDynInsertOp::create(rewriter, store.getLoc(),
                                                  parentScalar, parent,
                                                  selected, selection.index);
          if (parentType != parentScalar)
            selected = SimPackedUnflattenOp::create(rewriter, store.getLoc(),
                                                    parentType, selected);
        } else if (selection.index)
          selected = SimArrayDynInsertOp::create(
              rewriter, store.getLoc(), selection.parent.getType(),
              selection.parent, selected, selection.index);
        else if (selection.unionMember)
          selected = SimUnionConstructOp::create(rewriter, store.getLoc(),
                                                 selection.parent.getType(),
                                                 selected, selection.ordinal);
        else
          selected = SimAggregateInsertOp::create(
              rewriter, store.getLoc(), selection.parent.getType(),
              selection.parent, selected, selection.ordinal);
      auto replacement = SimRefStoreOp::create(
          rewriter, store.getLoc(), selected, allocation.getResult());
      replacement->setAttrs(store->getAttrs());
      rewriter.eraseOp(store);
    }
    for (Operation *view : llvm::reverse(selectedViews))
      if (view->use_empty())
        rewriter.eraseOp(view);
    return success();
  }
};

// Expose packed-local accesses to ordinary mem2reg. A sliced reference
// otherwise keeps an entire automatic allocation alive even when no reference
// escapes the function (IEEE 1800-2023 6.21). Preserve whole-value dependencies
// for overlapping slices and invalid indices (11.5.1); destructuring them into
// independent slots would be unsound.
template <typename View>
struct PromotePackedLocalView final : OpRewritePattern<View> {
  using OpRewritePattern<View>::OpRewritePattern;

  LogicalResult matchAndRewrite(View op,
                                PatternRewriter &rewriter) const override {
    auto allocation = op.getInput().template getDefiningOp<SimRefAllocOp>();
    if (!allocation || allocation.getPromotableSlots().empty())
      return failure();
    Type rootType = allocation.getResult().getType().getElementType();
    Type partType = op.getResult().getType().getElementType();
    Type rootScalar = getPackedScalarType(rootType);
    Type partScalar = getPackedScalarType(partType);
    auto rootWidth = getPackedWidth(rootType);
    auto partWidth = getPackedWidth(partType);
    if (!rootScalar || !partScalar || !rootWidth || !partWidth)
      return failure();
    Type planePartScalar = partScalar;
    if (isa<LogicType>(rootScalar) && isa<IntegerType>(partScalar))
      planePartScalar = LogicType::get(op.getContext(), *partWidth);
    else if (isa<LogicType>(rootScalar) != isa<LogicType>(partScalar))
      return failure();
    if constexpr (std::is_same_v<View, SimRefExtractOp>)
      if (op.getLowBit() > *rootWidth ||
          *partWidth > *rootWidth - op.getLowBit())
        return failure();
    // No NBA, event control, ref argument, spawn, return, force or other
    // reference escape may observe these read/modify/write intermediates.
    SmallVector<Value> pending{allocation.getResult()};
    llvm::SmallDenseSet<Value, 8> visited;
    while (!pending.empty()) {
      Value reference = pending.pop_back_val();
      if (!visited.insert(reference).second)
        continue;
      for (Operation *user : reference.getUsers()) {
        if (user->getParentOp() != allocation->getParentOp())
          return failure();
        if (isa<SimRefExtractOp, SimRefDynExtractOp>(user)) {
          pending.push_back(user->getResult(0));
          continue;
        }
        if (auto load = dyn_cast<SimRefLoadOp>(user)) {
          if (load.getReference() == reference)
            continue;
        }
        if (auto store = dyn_cast<SimRefStoreOp>(user)) {
          if (store.getReference() == reference)
            continue;
        }
        return failure();
      }
    }
    if (!llvm::all_of(op.getResult().getUsers(), [](Operation *user) {
          return isa<SimRefLoadOp, SimRefStoreOp>(user);
        }))
      return failure(); // Nested views are flattened by the adjacent pattern.
    SmallVector<Operation *> users(op.getResult().getUsers());
    for (Operation *user : users) {
      rewriter.setInsertionPoint(user);
      Value current = SimRefLoadOp::create(rewriter, user->getLoc(), rootType,
                                           allocation.getResult());
      if (rootType != rootScalar)
        current = SimPackedFlattenOp::create(rewriter, user->getLoc(),
                                             rootScalar, current);
      if (auto load = dyn_cast<SimRefLoadOp>(user)) {
        Value part;
        if constexpr (std::is_same_v<View, SimRefExtractOp>) {
          // Fixed two-state views are canonicalized with bits.dyn_extract,
          // whose normalized constant index folds to the equivalent slice.
          if (isa<IntegerType>(rootScalar)) {
            Value low = arith::ConstantIntOp::create(rewriter, load.getLoc(),
                                                     op.getLowBit(), 64);
            part = SimBitsDynExtractOp::create(rewriter, load.getLoc(),
                                               planePartScalar, current, low);
          } else
            part = SimLogicExtractOp::create(rewriter, load.getLoc(),
                                             planePartScalar, current,
                                             op.getLowBitAttr());
        } else {
          if (isa<IntegerType>(rootScalar))
            part = SimBitsDynExtractOp::create(rewriter, load.getLoc(),
                                               planePartScalar, current,
                                               op.getLowBit());
          else
            part = SimLogicDynExtractOp::create(rewriter, load.getLoc(),
                                                planePartScalar, current,
                                                op.getLowBit());
        }
        // LRM 6.11.2, 7.3.1: a mixed packed union stores four-state bits.
        // Reading a two-state member converts X/Z to zero; writing that
        // member inserts known bits while preserving the other union bits.
        if (planePartScalar != partScalar)
          part = SimLogicToBitsOp::create(rewriter, load.getLoc(), partScalar,
                                          part);
        if (partType != partScalar)
          part = SimPackedUnflattenOp::create(rewriter, load.getLoc(), partType,
                                              part);
        rewriter.replaceOp(load, part);
      } else {
        auto store = cast<SimRefStoreOp>(user);
        Value part = store.getValue();
        if (partType != partScalar)
          part = SimPackedFlattenOp::create(rewriter, store.getLoc(),
                                            partScalar, part);
        if (planePartScalar != partScalar)
          part = SimLogicFromBitsOp::create(rewriter, store.getLoc(),
                                            planePartScalar, part);
        Value merged;
        Value low;
        if constexpr (std::is_same_v<View, SimRefExtractOp>) {
          if (isa<LogicType>(rootScalar))
            merged =
                SimLogicInsertOp::create(rewriter, store.getLoc(), rootScalar,
                                         current, part, op.getLowBitAttr());
          else
            low = arith::ConstantIntOp::create(rewriter, store.getLoc(),
                                               op.getLowBit(), 64);
        } else
          low = op.getLowBit();
        if (!merged) {
          if (isa<IntegerType>(rootScalar))
            merged = SimBitsDynInsertOp::create(rewriter, store.getLoc(),
                                                rootScalar, current, part, low);
          else
            merged = SimLogicDynInsertOp::create(
                rewriter, store.getLoc(), rootScalar, current, part, low);
        }
        if (rootType != rootScalar)
          merged = SimPackedUnflattenOp::create(rewriter, store.getLoc(),
                                                rootType, merged);
        auto replacement = SimRefStoreOp::create(
            rewriter, store.getLoc(), merged, allocation.getResult());
        replacement->setAttrs(store->getAttrs());
        rewriter.eraseOp(store);
      }
    }
    rewriter.eraseOp(op);
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
    if (auto splat = op.getInput().getDefiningOp<SimAggregateSplatOp>()) {
      rewriter.replaceOp(op, splat.getInput());
      return success();
    }
    if (auto construct =
            op.getInput().getDefiningOp<SimAggregateConstructOp>()) {
      rewriter.replaceOp(op, construct.getElements()[index]);
      return success();
    }
    if (op.getInput().getDefiningOp<SimAggregateDefaultOp>()) {
      // IEEE 1800-2023 7.2.2: an initialized member defaults to its
      // initializer rather than to its type's default.
      Value value;
      if (auto record = dyn_cast<UnpackedStructType>(op.getInput().getType()))
        if (auto initializer = dyn_cast_or_null<FrozenConstantAttr>(
                cast<FieldAttr>(record.getFields()[index]).getInitializer())) {
          FailureOr<Value> materialized =
              materializeFrozenConstant(rewriter, op.getLoc(), initializer);
          if (failed(materialized))
            return failure();
          value = *materialized;
        }
      if (!value)
        value = materializeDefaultValue(rewriter, op.getLoc(),
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
      // Keep a shared packed word as a value. Narrowing several extracts into
      // separate reference loads duplicates reads and prevents whole-word SSA
      // sharing after local promotion (LRM 7.4.1). A sole extract can still
      // narrow its read, and large aggregates retain field-address lowering.
      if (auto width = getPackedWidth(load.getResult().getType());
          width && *width <= 64 && !load.getResult().hasOneUse())
        return failure();
      // Preserve the whole automatic value for mem2reg (LRM 6.21, 7.4.5).
      if (auto allocation = load.getReference().getDefiningOp<SimRefAllocOp>();
          allocation && !allocation.getPromotableSlots().empty() &&
          getProvenanceSpan(allocation.getInitialValue().getType()) <= 64)
        return failure();
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
    // A one-element splat canonicalizes to construct, so require at least two
    // elements here. This represents repeated SSA values without duplication.
    if (isa<PackedArrayType, UnpackedArrayType>(op.getType()) &&
        op.getElements().size() > 1 &&
        isa<IntegerType, LogicType>(op.getElements().front().getType()) &&
        llvm::all_equal(op.getElements())) {
      rewriter.replaceOpWithNewOp<SimAggregateSplatOp>(
          op, op.getType(), op.getElements().front());
      return success();
    }
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

struct SimplifyAggregateSplat final : OpRewritePattern<SimAggregateSplatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateSplatOp op,
                                PatternRewriter &rewriter) const override {
    if (getAggregateNumElements(op.getResult().getType()) != 1)
      return failure();
    rewriter.replaceOp(op, SimAggregateConstructOp::create(
                               rewriter, op.getLoc(), op.getResult().getType(),
                               ValueRange{op.getInput()}));
    return success();
  }
};

struct FoldNullRecursiveExportBitstream final
    : OpRewritePattern<SimRecursiveExportBitstreamOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimRecursiveExportBitstreamOp op,
                                PatternRewriter &rewriter) const override {
    bool managedNull =
        static_cast<bool>(op.getInput().getDefiningOp<SimManagedNullOp>());
    bool classNull =
        static_cast<bool>(op.getInput().getDefiningOp<SimClassNullOp>());
    if (!managedNull && !classNull)
      return failure();
    // Object legality is a static type property even when this particular
    // value is null.  Only fold after class planning has validated the full
    // schema closure and assigned a trusted site.
    if (classNull && !op.getClassSiteIdAttr())
      return failure();
    Value result;
    if (auto integer = dyn_cast<IntegerType>(op.getResult().getType())) {
      result = arith::ConstantOp::create(
          rewriter, op.getLoc(), integer,
          rewriter.getIntegerAttr(integer, APInt::getZero(integer.getWidth())));
    } else if (auto logic = dyn_cast<LogicType>(op.getResult().getType())) {
      auto plane = IntegerType::get(rewriter.getContext(), logic.getWidth());
      result = SimLogicConstantOp::create(rewriter, op.getLoc(), logic,
                                          rewriter.getIntegerAttr(plane, 0),
                                          rewriter.getIntegerAttr(plane, 0));
    } else {
      return failure();
    }
    Value matched =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                  rewriter.getBoolAttr(false));
    Value watch = SimManagedWatchNullOp::create(
        rewriter, op.getLoc(), ManagedWatchType::get(rewriter.getContext()));
    rewriter.replaceOp(op, ValueRange{result, matched, watch});
    return success();
  }
};

struct FoldSmallAggregateExportBitstream final
    : OpRewritePattern<SimAggregateExportBitstreamOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateExportBitstreamOp op,
                                PatternRewriter &rewriter) const override {
    constexpr unsigned maximumLeaves = 16;
    constexpr unsigned maximumWidth = 1024;
    std::optional<unsigned> resultWidth =
        getPackedWidth(op.getResult().getType());
    if (!resultWidth || *resultWidth > maximumWidth)
      return failure();
    SmallVector<Value> leaves;
    std::function<bool(Value)> collect = [&](Value value) {
      Type type = value.getType();
      if (Type scalar = getPackedScalarType(type)) {
        if (leaves.size() == maximumLeaves)
          return false;
        leaves.push_back(value);
        return true;
      }
      auto construct = value.getDefiningOp<SimAggregateConstructOp>();
      if (!construct ||
          leaves.size() + construct.getElements().size() > maximumLeaves)
        return false;
      for (Value element : construct.getElements())
        if (!collect(element))
          return false;
      return true;
    };
    if (!collect(op.getInput()) || leaves.empty())
      return failure();
    SmallVector<Type> scalarTypes;
    uint64_t totalWidth = 0;
    for (Value leaf : leaves) {
      Type scalar = getPackedScalarType(leaf.getType());
      std::optional<unsigned> width = getPackedWidth(scalar);
      if (!scalar || !width || !isa<IntegerType, LogicType>(scalar) ||
          *width > std::numeric_limits<uint64_t>::max() - totalWidth)
        return failure();
      scalarTypes.push_back(scalar);
      totalWidth += *width;
    }
    if (totalWidth != *resultWidth)
      return failure();
    for (auto [index, scalar] : llvm::enumerate(scalarTypes))
      if (scalar != leaves[index].getType())
        leaves[index] = SimPackedFlattenOp::create(rewriter, op.getLoc(),
                                                   scalar, leaves[index]);

    if (isa<LogicType>(op.getResult().getType())) {
      SmallVector<Value> logicLeaves;
      for (Value leaf : leaves) {
        if (isa<LogicType>(leaf.getType())) {
          logicLeaves.push_back(leaf);
          continue;
        }
        auto integer = cast<IntegerType>(leaf.getType());
        logicLeaves.push_back(SimLogicFromBitsOp::create(
            rewriter, op.getLoc(),
            LogicType::get(op.getContext(), integer.getWidth()), leaf));
      }
      rewriter.replaceOpWithNewOp<SimLogicConcatOp>(
          op, op.getResult().getType(), logicLeaves);
      return success();
    }

    auto resultType = cast<IntegerType>(op.getResult().getType());
    Value assembled =
        arith::ConstantOp::create(rewriter, op.getLoc(), resultType,
                                  rewriter.getIntegerAttr(resultType, 0));
    unsigned trailing = *resultWidth;
    for (Value leaf : leaves) {
      if (auto logic = dyn_cast<LogicType>(leaf.getType()))
        leaf = SimLogicToBitsOp::create(
            rewriter, op.getLoc(),
            IntegerType::get(op.getContext(), logic.getWidth()), leaf);
      auto integer = cast<IntegerType>(leaf.getType());
      trailing -= integer.getWidth();
      Value extended = integer == resultType
                           ? leaf
                           : Value(arith::ExtUIOp::create(rewriter, op.getLoc(),
                                                          resultType, leaf));
      if (trailing) {
        Value amount = arith::ConstantOp::create(
            rewriter, op.getLoc(), resultType,
            rewriter.getIntegerAttr(resultType, trailing));
        extended =
            arith::ShLIOp::create(rewriter, op.getLoc(), extended, amount);
      }
      assembled =
          arith::OrIOp::create(rewriter, op.getLoc(), assembled, extended);
    }
    assert(trailing == 0 && "prevalidated fixed bit-stream width changed");
    rewriter.replaceOp(op, assembled);
    return success();
  }
};

struct SimplifyAggregateInsert final : OpRewritePattern<SimAggregateInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimAggregateInsertOp op,
                                PatternRewriter &rewriter) const override {
    if (auto construct = op.getInput().getDefiningOp<SimAggregateConstructOp>();
        construct && construct->hasOneUse()) {
      SmallVector<Value> elements(construct.getElements());
      elements[op.getIndex()] = op.getReplacement();
      rewriter.replaceOpWithNewOp<SimAggregateConstructOp>(op, op.getType(),
                                                           elements);
      return success();
    }
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

struct ConstantArrayInsert final : OpRewritePattern<SimArrayDynInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynInsertOp op,
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
    // IEEE 1800-2023 7.4.5: a write through an invalid index performs no
    // operation, so the array passes through unchanged.
    if (!ordinal) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }
    auto replacement = SimAggregateInsertOp::create(
        rewriter, op.getLoc(), op.getResult().getType(), op.getInput(),
        op.getReplacement(), rewriter.getI64IntegerAttr(*ordinal));
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
// and selecting out of the loaded value, mirroring what
// SimplifyAggregateExtract does for a constant ordinal. Without this a
// dynamically indexed read of a large array becomes a multi-kilobit SSA value
// and a full-width shift, which is quadratic to analyze and needlessly slow to
// run.
struct DynamicArrayExtractThroughReference final
    : OpRewritePattern<SimArrayDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto load = op.getInput().getDefiningOp<SimRefLoadOp>();
    if (!load)
      return failure();
    // Keep automatic values exposed to mem2reg (LRM 6.21, 7.4.5).
    // Reintroducing a reference here would undo PromoteLocalArrayView.
    if (auto allocation = load.getReference().getDefiningOp<SimRefAllocOp>();
        allocation && !allocation.getPromotableSlots().empty() &&
        getProvenanceSpan(allocation.getInitialValue().getType()) <= 64)
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

struct FoldStringCompare final : OpRewritePattern<SimStringCompareOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimStringCompareOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getLhs() == op.getRhs()) {
      rewriter.replaceOpWithNewOp<arith::ConstantOp>(
          op, rewriter.getI32Type(), rewriter.getI32IntegerAttr(0));
      return success();
    }
    auto lhs = op.getLhs().getDefiningOp<SimStringLiteralOp>();
    auto rhs = op.getRhs().getDefiningOp<SimStringLiteralOp>();
    if (!lhs || !rhs)
      return failure();

    StringRef left = lhs.getValue();
    StringRef right = rhs.getValue();
    size_t common = std::min(left.size(), right.size());
    int32_t result = 0;
    for (size_t index = 0; index != common; ++index) {
      unsigned char a = static_cast<unsigned char>(left[index]);
      unsigned char b = static_cast<unsigned char>(right[index]);
      if (op.getCaseInsensitive()) {
        if (a >= 'A' && a <= 'Z')
          a = static_cast<unsigned char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
          b = static_cast<unsigned char>(b + ('a' - 'A'));
      }
      if (a == b)
        continue;
      result = a < b ? -1 : 1;
      break;
    }
    if (result == 0 && left.size() != right.size())
      result = left.size() < right.size() ? -1 : 1;
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, rewriter.getI32Type(), rewriter.getI32IntegerAttr(result));
    return success();
  }
};

struct FoldStringLength final : OpRewritePattern<SimStringLengthOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimStringLengthOp op,
                                PatternRewriter &rewriter) const override {
    auto literal = op.getInput().getDefiningOp<SimStringLiteralOp>();
    if (!literal)
      return failure();

    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, rewriter.getI64Type(),
        rewriter.getI64IntegerAttr(literal.getValue().size()));
    return success();
  }
};

struct FoldStringGetc final : OpRewritePattern<SimStringGetcOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimStringGetcOp op,
                                PatternRewriter &rewriter) const override {
    auto literal = op.getInput().getDefiningOp<SimStringLiteralOp>();
    APInt index;
    if (!literal || !matchPattern(op.getIndex(), m_ConstantInt(&index)))
      return failure();
    // IEEE 1800-2023 6.16.3: negative and out-of-range indices return zero.
    uint8_t byte = 0;
    if (!index.isNegative() && index.ult(literal.getValue().size()))
      byte = static_cast<uint8_t>(literal.getValue()[index.getZExtValue()]);
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, rewriter.getI8Type(), rewriter.getI8IntegerAttr(byte));
    return success();
  }
};

struct FoldStringToPacked final : OpRewritePattern<SimStringToPackedOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimStringToPackedOp op,
                                PatternRewriter &rewriter) const override {
    auto literal = op.getInput().getDefiningOp<SimStringLiteralOp>();
    if (!literal)
      return failure();
    auto type = cast<IntegerType>(op.getType());
    if (!type.isSignless())
      return failure();
    APInt value(type.getWidth(), 0);
    // IEEE 1800-2023 6.16: right justify, truncate high bytes, zero extend.
    // Visit only bytes that survive truncation, including a partial high byte.
    uint64_t bytes = llvm::divideCeil(type.getWidth(), 8u);
    StringRef suffix = literal.getValue().take_back(bytes);
    for (auto [index, byte] : llvm::enumerate(llvm::reverse(suffix.bytes())))
      value |= APInt(type.getWidth(), byte).shl(index * 8);
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, type, IntegerAttr::get(type, value));
    return success();
  }
};

struct FoldExactStringToPacked final
    : OpRewritePattern<SimStringToPackedExactOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimStringToPackedExactOp op,
                                PatternRewriter &rewriter) const override {
    auto literal = op.getInput().getDefiningOp<SimStringLiteralOp>();
    auto resultType = dyn_cast<IntegerType>(op.getResult().getType());
    if (!literal || !resultType || resultType.getWidth() > 1024 ||
        (resultType.getWidth() % 8) != 0 ||
        literal.getValue().size() != resultType.getWidth() / 8)
      return failure();

    APInt result = APInt::getZero(resultType.getWidth());
    for (auto [index, byte] : llvm::enumerate(literal.getValue().bytes())) {
      unsigned shift = resultType.getWidth() - (index + 1) * 8;
      result |= APInt(resultType.getWidth(), byte).shl(shift);
    }
    Value packed =
        arith::ConstantOp::create(rewriter, op.getLoc(), resultType,
                                  rewriter.getIntegerAttr(resultType, result));
    Value matched =
        arith::ConstantOp::create(rewriter, op.getLoc(), rewriter.getI1Type(),
                                  rewriter.getBoolAttr(true));
    rewriter.replaceOp(op, ValueRange{packed, matched});
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Two-state integer helpers. These build exact arith for values whose
// unknown plane is known to be zero; no operation here can produce poison.
//===----------------------------------------------------------------------===//

/// Bits [low, low + width) of `bits`.
static Value extractIntegerBits(PatternRewriter &rewriter, Location location,
                                Value bits, uint64_t low, unsigned width) {
  auto type = cast<IntegerType>(bits.getType());
  if (low != 0)
    bits = rewriter.createOrFold<arith::ShRUIOp>(
        location, bits,
        createIntegerConstant(rewriter, location, type,
                              APInt(type.getWidth(), low)));
  if (width < type.getWidth())
    bits = rewriter.createOrFold<arith::TruncIOp>(
        location, rewriter.getIntegerType(width), bits);
  return bits;
}

static Value populationCount(PatternRewriter &rewriter, Location location,
                             Value bits) {
  return math::CtPopOp::create(rewriter, location, bits.getType(), bits);
}

/// Resize an unsigned count to the 32-bit SystemVerilog `int` result. The
/// count of a W-bit value fits in W bits, and truncation is modulo 2^32.
static Value resizeCount(PatternRewriter &rewriter, Location location,
                         Value count) {
  auto type = cast<IntegerType>(count.getType());
  auto i32 = rewriter.getI32Type();
  if (type.getWidth() > 32)
    return rewriter.createOrFold<arith::TruncIOp>(location, i32, count);
  if (type.getWidth() < 32)
    return rewriter.createOrFold<arith::ExtUIOp>(location, i32, count);
  return count;
}

/// Materialize an operand known to be two-state: a from_bits input, an
/// integer, or a logic constant without X/Z.
static std::optional<Value> getTwoStateBits(Value value, Location location,
                                            PatternRewriter &rewriter) {
  if (isa<IntegerType>(value.getType()))
    return value;
  std::optional<OpFoldResult> bits = matchTwoStateValue(value);
  if (!bits)
    return std::nullopt;
  return materializeBits(*bits, location, rewriter);
}

static bool isTwoStateValue(Value value) {
  return isa<IntegerType>(value.getType()) ||
         matchTwoStateValue(value).has_value();
}

//===----------------------------------------------------------------------===//
// Control predicates
//===----------------------------------------------------------------------===//

/// IEEE 1800-2023 12.4 and 11.4.7: a condition holds only when its value has
/// a known one bit. Express truth tests of logical connectives, negations,
/// reductions and equalities as two-state predicates so a branch leaves the
/// four-state domain as early as possible.
struct SimplifyTruthCondition final : OpRewritePattern<SimLogicIsTrueOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicIsTrueOp op,
                                PatternRewriter &rewriter) const override {
    Operation *producer = op.getInput().getDefiningOp();
    if (!producer)
      return failure();
    Location location = op.getLoc();
    auto truth = [&](Value value) -> Value {
      return SimLogicIsTrueOp::create(rewriter, location, value);
    };
    // x === c is one exactly when every bit of x is the known bit of c.
    auto caseEqual = [&](Value value, Value constant) -> Value {
      return SimLogicCompareOp::create(rewriter, location, rewriter.getI1Type(),
                                       CompareKind::CaseEq, value, constant);
    };
    auto caseEqualBits = [&](Value value, const APInt &bits) -> Value {
      return caseEqual(value, createKnownLogic(rewriter, location,
                                               bits.getBitWidth(), bits));
    };
    Value replacement;
    // Splitting a connective into two truth tests only helps when the
    // four-state result is not needed anyway; one-for-one rewrites below
    // apply regardless.
    bool soleUse = producer->hasOneUse();
    if (auto logical = dyn_cast<SimLogicLogicalOp>(producer);
        logical && soleUse) {
      // Kleene conjunction is one only for two true operands; disjunction is
      // one when either operand is true.
      Value lhs = truth(logical.getLhs());
      Value rhs = truth(logical.getRhs());
      replacement =
          logical.getKind() == LogicalKind::And
              ? Value(arith::AndIOp::create(rewriter, location, lhs, rhs))
              : Value(arith::OrIOp::create(rewriter, location, lhs, rhs));
    } else if (auto binary = dyn_cast<SimLogicBinaryOp>(producer);
               binary && soleUse) {
      // A bit of a | b is a known one when either operand bit is (Table
      // 11-13); a one-bit a & b is a known one only when both are.
      bool single = binary.getType().getWidth() == 1;
      if (binary.getKind() == BinaryKind::Or)
        replacement = arith::OrIOp::create(
            rewriter, location, truth(binary.getLhs()), truth(binary.getRhs()));
      else if (binary.getKind() == BinaryKind::And && single)
        replacement = arith::AndIOp::create(
            rewriter, location, truth(binary.getLhs()), truth(binary.getRhs()));
    } else if (auto unary = dyn_cast<SimLogicUnaryOp>(producer)) {
      // !x is one exactly when every bit of x is a known zero.
      if (isTruthNegation(unary)) {
        Value source = stripTruthPreservingProducers(unary.getInput());
        replacement = caseEqualBits(
            source,
            APInt::getZero(cast<LogicType>(source.getType()).getWidth()));
      }
    } else if (auto reduction = dyn_cast<SimLogicReductionOp>(producer)) {
      // Tables 11-16 and 11-17: &x is one only when every bit is a known one
      // and ~|x only when every bit is a known zero.
      unsigned width = reduction.getInput().getType().getWidth();
      if (reduction.getKind() == ReductionKind::And) {
        replacement =
            caseEqualBits(reduction.getInput(), APInt::getAllOnes(width));
      } else if (reduction.getKind() == ReductionKind::Nor) {
        Value source = stripTruthPreservingProducers(reduction.getInput());
        replacement = caseEqualBits(
            source,
            APInt::getZero(cast<LogicType>(source.getType()).getWidth()));
      }
    } else if (auto compare = dyn_cast<SimLogicCompareOp>(producer);
               compare && compare.getKind() == CompareKind::Eq) {
      // 11.4.5: x == c is one only when neither operand has X or Z and the
      // bits match. A constant with X or Z therefore never compares true.
      for (unsigned operand : {1u, 0u}) {
        Value constant = compare->getOperand(operand);
        std::optional<LogicPlanes> planes = getConstantPlanes(constant);
        if (!planes)
          continue;
        replacement =
            planes->unknown.isZero()
                ? caseEqual(compare->getOperand(1 - operand), constant)
                : Value(arith::ConstantOp::create(rewriter, location,
                                                  rewriter.getBoolAttr(false)));
        break;
      }
    }
    if (!replacement)
      return failure();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Two-state operations
//===----------------------------------------------------------------------===//

/// IEEE 1800-2023 11.4.10 on a known operand shifted by a known amount.
/// Amounts of the width or more are folded on the shift itself, so the
/// integer shift is in range and cannot form poison. A variable amount stays
/// a logic shift: its range guard would cost more operations than the shift.
struct TwoStateShift final : OpRewritePattern<SimLogicShiftOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicShiftOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    std::optional<ConstantIndex> amount = getConstantIndex(op.getAmount());
    unsigned width = op.getType().getWidth();
    if (!amount || !amount->value || amount->value->uge(width) ||
        !isTwoStateValue(op.getInput()))
      return failure();
    Value value = *getTwoStateBits(op.getInput(), location, rewriter);
    auto type = cast<IntegerType>(value.getType());
    Value shift = createIntegerConstant(
        rewriter, location, type, APInt(width, amount->value->getZExtValue()));
    Value result;
    switch (op.getKind()) {
    case ShiftKind::Left:
      result = rewriter.createOrFold<arith::ShLIOp>(location, value, shift);
      break;
    case ShiftKind::Right:
      result = rewriter.createOrFold<arith::ShRUIOp>(location, value, shift);
      break;
    case ShiftKind::RightArith:
      result = rewriter.createOrFold<arith::ShRSIOp>(location, value, shift);
      break;
    }
    replaceWithNewOp(
        rewriter, op,
        SimLogicFromBitsOp::create(rewriter, location, op.getType(), result));
    return success();
  }
};

/// The low slice of a known value is an integer truncation. Other slices stay
/// logic extracts, which the bytecode backend executes as one instruction.
struct TwoStateExtract final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getLowBit() != 0 || !isTwoStateValue(op.getInput()))
      return failure();
    Value bits = *getTwoStateBits(op.getInput(), op.getLoc(), rewriter);
    bits = extractIntegerBits(rewriter, op.getLoc(), bits, op.getLowBit(),
                              op.getType().getWidth());
    replaceWithNewOp(
        rewriter, op,
        SimLogicFromBitsOp::create(rewriter, op.getLoc(), op.getType(), bits));
    return success();
  }
};

/// An unknown or wholly invalid index leaves the input unchanged and a
/// partial write stores only in-range replacement bits, so a write of known
/// bits into known bits stays known (11.5.1).
struct TwoStateDynamicInsert final : OpRewritePattern<SimLogicDynInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicDynInsertOp op,
                                PatternRewriter &rewriter) const override {
    if (!isTwoStateValue(op.getInput()) ||
        !isTwoStateValue(op.getReplacement()))
      return failure();
    Value base = *getTwoStateBits(op.getInput(), op.getLoc(), rewriter);
    Value piece = *getTwoStateBits(op.getReplacement(), op.getLoc(), rewriter);
    Value bits = SimBitsDynInsertOp::create(
        rewriter, op.getLoc(), base.getType(), base, piece, op.getLowBit());
    replaceWithNewOp(
        rewriter, op,
        SimLogicFromBitsOp::create(rewriter, op.getLoc(), op.getType(), bits));
    return success();
  }
};

/// Parity of a known word is the low bit of its population count.
struct TwoStateParity final : OpRewritePattern<SimLogicReductionOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReductionOp op,
                                PatternRewriter &rewriter) const override {
    if ((op.getKind() != ReductionKind::Xor &&
         op.getKind() != ReductionKind::Xnor) ||
        !isTwoStateValue(op.getInput()))
      return failure();
    Location location = op.getLoc();
    Value bits = *getTwoStateBits(op.getInput(), location, rewriter);
    Value parity = bits;
    if (cast<IntegerType>(bits.getType()).getWidth() != 1)
      parity =
          arith::TruncIOp::create(rewriter, location, rewriter.getI1Type(),
                                  populationCount(rewriter, location, bits));
    if (op.getKind() == ReductionKind::Xnor)
      parity = arith::XOrIOp::create(
          rewriter, location, parity,
          arith::ConstantOp::create(rewriter, location,
                                    rewriter.getBoolAttr(true)));
    replaceWithNewOp(
        rewriter, op,
        SimLogicFromBitsOp::create(rewriter, location, op.getType(), parity));
    return success();
  }
};

/// IEEE 1800-2023 20.9 with constant controls. On a two-state input only 0
/// and 1 can match. On a four-state input, the known ones of x are the bits
/// of to_bits(x) and its known zeros are the bits of to_bits(~x), since
/// bitwise negation maps X and Z to X (Table 11-15).
struct CountBitsWithPopulationCount final
    : OpRewritePattern<SimLogicCountBitsOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCountBitsOp op,
                                PatternRewriter &rewriter) const override {
    Value input = op.getInput();
    if (!isa<IntegerType, LogicType>(input.getType()))
      return failure();
    bool selected[4] = {};
    for (Value control : op.getControls()) {
      std::optional<LogicPlanes> planes = getConstantPlanes(control);
      if (!planes)
        return failure();
      selected[(planes->unknown[0] ? 2u : 0u) | (planes->value[0] ? 1u : 0u)] =
          true;
    }
    std::optional<unsigned> packedWidth = getPackedWidth(input.getType());
    if (!packedWidth)
      return failure();
    Location location = op.getLoc();
    unsigned width = *packedWidth;
    auto type = rewriter.getIntegerType(width);
    Value ones;
    Value zeros;
    if (isTwoStateValue(input)) {
      Value bits = *getTwoStateBits(input, location, rewriter);
      if (selected[0] && selected[1]) {
        rewriter.replaceOp(op, createIntegerConstant(rewriter, location,
                                                     op.getType(),
                                                     APInt(32, width)));
        return success();
      }
      if (selected[1])
        ones = populationCount(rewriter, location, bits);
      if (selected[0])
        zeros = arith::SubIOp::create(
            rewriter, location,
            createIntegerConstant(rewriter, location, type,
                                  APInt(width, width)),
            populationCount(rewriter, location, bits));
    } else {
      // Counting both known values would take two conversions and two counts.
      if (selected[2] || selected[3] || (selected[0] && selected[1]))
        return failure();
      if (selected[1])
        ones = populationCount(
            rewriter, location,
            SimLogicToBitsOp::create(rewriter, location, type, input));
      if (selected[0]) {
        Value inverted = SimLogicUnaryOp::create(
            rewriter, location, input.getType(), UnaryKind::BitNot, input);
        zeros = populationCount(
            rewriter, location,
            SimLogicToBitsOp::create(rewriter, location, type, inverted));
      }
    }
    Value count =
        ones && zeros
            ? Value(arith::AddIOp::create(rewriter, location, ones, zeros))
        : ones  ? ones
        : zeros ? zeros
                : createIntegerConstant(rewriter, location, type,
                                        APInt::getZero(width));
    rewriter.replaceOp(op, resizeCount(rewriter, location, count));
    return success();
  }
};

/// to_bits keeps exactly the known one bits (6.24.1). A bit of a & b is a
/// known one only when both operand bits are, and a bit of a | b when either
/// is (Tables 11-12, 11-13). With one known operand the conversion moves to
/// the other one and the operation becomes integer arith without adding
/// operations; a one-use producer keeps four-state work from being repeated.
struct ConvertLogicToBitsThroughProducer final
    : OpRewritePattern<SimLogicToBitsOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicToBitsOp op,
                                PatternRewriter &rewriter) const override {
    auto binary = op.getInput().getDefiningOp<SimLogicBinaryOp>();
    if (!binary || !binary->hasOneUse() || !op.getType().isSignless() ||
        (binary.getKind() != BinaryKind::And &&
         binary.getKind() != BinaryKind::Or) ||
        (!isTwoStateValue(binary.getLhs()) &&
         !isTwoStateValue(binary.getRhs())))
      return failure();
    Location location = op.getLoc();
    auto toBits = [&](Value value) -> Value {
      if (std::optional<Value> bits =
              getTwoStateBits(value, location, rewriter))
        return *bits;
      return SimLogicToBitsOp::create(rewriter, location, op.getType(), value);
    };
    Value lhs = toBits(binary.getLhs());
    Value rhs = toBits(binary.getRhs());
    if (binary.getKind() == BinaryKind::And)
      rewriter.replaceOpWithNewOp<arith::AndIOp>(op, lhs, rhs);
    else
      rewriter.replaceOpWithNewOp<arith::OrIOp>(op, lhs, rhs);
    return success();
  }
};

/// A two-state selection at constant index zero is an integer truncation
/// (11.5.1). Other constant selections keep the one-instruction bytecode
/// extract; the native backend simplifies them after lowering.
struct ConstantBitsDynamicExtract final
    : OpRewritePattern<SimBitsDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimBitsDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    std::optional<ConstantIndex> index = getConstantIndex(op.getLowBit());
    if (!index || !index->value || !index->value->isZero())
      return failure();
    rewriter.replaceOp(op,
                       extractIntegerBits(rewriter, op.getLoc(), op.getInput(),
                                          0, op.getType().getWidth()));
    return success();
  }
};

struct SimplifyBitsExtractSource final
    : OpRewritePattern<SimBitsDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimBitsDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto index = getConstantIndex(op.getLowBit());
    uint64_t low;
    if (!index || !index->value ||
        !isKnownInRangeIndex(*index->value, op.getInput().getType().getWidth(),
                             op.getType().getWidth(), low))
      return failure();
    auto source = getBitsInsertSlice(op.getInput(), low, op.getType().getWidth());
    if (!source)
      return failure();
    rewriter.replaceOp(op, extractIntegerBits(rewriter, op.getLoc(),
                                              source->first, source->second,
                                              op.getType().getWidth()));
    return success();
  }
};

/// Integer packed selections also use shrui/trunci. Register this cross-dialect
/// rule on the simulation dialect, leaving unrelated arith operations alone.
struct ExtractBitsInsertWithTruncation final
    : OpRewritePattern<arith::TruncIOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(arith::TruncIOp op,
                                PatternRewriter &rewriter) const override {
    auto resultType = dyn_cast<IntegerType>(op.getType());
    if (!resultType ||
        op.getOverflowFlags() != arith::IntegerOverflowFlags::none)
      return failure();
    Value input = op.getIn();
    uint64_t low = 0;
    if (auto shift = input.getDefiningOp<arith::ShRUIOp>()) {
      auto amount = getConstantIndex(shift.getRhs());
      if (shift.getIsExact() || !amount || !amount->value ||
          amount->value->uge(cast<IntegerType>(input.getType()).getWidth()))
        return failure();
      low = amount->value->getZExtValue();
      input = shift.getLhs();
    }
    auto source = getBitsInsertSlice(input, low, resultType.getWidth());
    if (!source)
      return failure();
    rewriter.replaceOp(op, extractIntegerBits(rewriter, op.getLoc(),
                                              source->first, source->second,
                                              resultType.getWidth()));
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Canonical forms. Each rule picks one spelling for operations the LRM
// defines identically, so CSE and the other patterns see a single form.
//===----------------------------------------------------------------------===//

/// A truncating resize keeps the low bits whatever its signedness (11.6.1).
struct TruncatingResizeToExtract final : OpRewritePattern<SimLogicResizeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicResizeOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getType().getWidth() >= op.getInput().getType().getWidth())
      return failure();
    replaceWithNewOp(rewriter, op,
                     SimLogicExtractOp::create(rewriter, op.getLoc(),
                                               op.getType(), op.getInput(),
                                               rewriter.getI64IntegerAttr(0)));
    return success();
  }
};

/// {'0, x} is the zero extension of x, and {zext(y), z...} is the zero
/// extension of {y, z...} (11.4.12, 11.6.1).
struct ConcatLeadingZerosToResize final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getInputs().size() < 2)
      return failure();
    Value leading = op.getInputs().front();
    SmallVector<Value> rest(op.getInputs().drop_front());
    if (auto resize = leading.getDefiningOp<SimLogicResizeOp>();
        resize && !resize.getIsSigned() &&
        resize.getType().getWidth() > resize.getInput().getType().getWidth())
      rest.insert(rest.begin(), resize.getInput());
    else if (!isKnownZero(leading))
      return failure();
    Value inner = rest.front();
    if (rest.size() > 1) {
      uint64_t width = 0;
      for (Value value : rest)
        width += cast<LogicType>(value.getType()).getWidth();
      inner = SimLogicConcatOp::create(
          rewriter, op.getLoc(), LogicType::get(op.getContext(), width), rest);
    }
    replaceWithNewOp(rewriter, op,
                     SimLogicResizeOp::create(rewriter, op.getLoc(),
                                              op.getType(), inner,
                                              rewriter.getBoolAttr(false)));
    return success();
  }
};

/// Adjacent constant operands of a concatenation form one constant.
struct MergeConcatConstants final : OpRewritePattern<SimLogicConcatOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicConcatOp op,
                                PatternRewriter &rewriter) const override {
    // A failing pattern must not create operations, so find an adjacent pair
    // of constants before building anything.
    auto inputsRange = op.getInputs();
    bool adjacent = false;
    for (unsigned index = 1; index < inputsRange.size() && !adjacent; ++index)
      adjacent = getConstantPlanes(inputsRange[index - 1]) &&
                 getConstantPlanes(inputsRange[index]);
    if (!adjacent)
      return failure();
    SmallVector<Value> inputs;
    std::optional<LogicPlanes> pending;
    bool changed = false;
    auto flush = [&]() {
      if (!pending)
        return;
      inputs.push_back(createLogicConstant(rewriter, op.getLoc(),
                                           pending->value.getBitWidth(),
                                           pending->value, pending->unknown));
      pending.reset();
    };
    for (Value input : op.getInputs()) {
      std::optional<LogicPlanes> planes = getConstantPlanes(input);
      if (!planes) {
        flush();
        inputs.push_back(input);
        continue;
      }
      if (!pending) {
        pending = planes;
        continue;
      }
      unsigned width =
          pending->value.getBitWidth() + planes->value.getBitWidth();
      unsigned shift = planes->value.getBitWidth();
      pending = LogicPlanes{pending->value.zext(width).shl(shift) |
                                planes->value.zext(width),
                            pending->unknown.zext(width).shl(shift) |
                                planes->unknown.zext(width)};
      changed = true;
    }
    if (!changed)
      return failure();
    flush();
    if (inputs.size() == 1) {
      rewriter.replaceOp(op, inputs.front());
      return success();
    }
    replaceWithNewOp(
        rewriter, op,
        SimLogicConcatOp::create(rewriter, op.getLoc(), op.getType(), inputs));
    return success();
  }
};

/// On one-bit operands, logical and bitwise connectives agree: each maps Z
/// to X and applies the Kleene table (Tables 11-12, 11-13; 11.4.7).
struct SingleBitLogicalToBitwise final : OpRewritePattern<SimLogicLogicalOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicLogicalOp op,
                                PatternRewriter &rewriter) const override {
    // Identities with constants or a repeated operand belong to
    // SimplifyLogical, which reduces them further.
    if (op.getLhs().getType().getWidth() != 1 ||
        op.getRhs().getType().getWidth() != 1 || op.getLhs() == op.getRhs() ||
        isConstantValue(op.getLhs()) || isConstantValue(op.getRhs()))
      return failure();
    replaceWithNewOp(
        rewriter, op,
        SimLogicBinaryOp::create(
            rewriter, op.getLoc(), op.getType(),
            op.getKind() == LogicalKind::And ? BinaryKind::And : BinaryKind::Or,
            op.getLhs(), op.getRhs()));
    return success();
  }
};

static std::optional<CompareKind> getInverseComparison(CompareKind kind) {
  switch (kind) {
  case CompareKind::Eq:
    return CompareKind::Ne;
  case CompareKind::Ne:
    return CompareKind::Eq;
  case CompareKind::ULT:
    return CompareKind::UGE;
  case CompareKind::ULE:
    return CompareKind::UGT;
  case CompareKind::UGT:
    return CompareKind::ULE;
  case CompareKind::UGE:
    return CompareKind::ULT;
  case CompareKind::SLT:
    return CompareKind::SGE;
  case CompareKind::SLE:
    return CompareKind::SGT;
  case CompareKind::SGT:
    return CompareKind::SLE;
  case CompareKind::SGE:
    return CompareKind::SLT;
  case CompareKind::WildEq:
    return CompareKind::WildNe;
  case CompareKind::WildNe:
    return CompareKind::WildEq;
  default:
    return std::nullopt;
  }
}

static ReductionKind getInverseReduction(ReductionKind kind) {
  switch (kind) {
  case ReductionKind::And:
    return ReductionKind::Nand;
  case ReductionKind::Nand:
    return ReductionKind::And;
  case ReductionKind::Or:
    return ReductionKind::Nor;
  case ReductionKind::Nor:
    return ReductionKind::Or;
  case ReductionKind::Xor:
    return ReductionKind::Xnor;
  case ReductionKind::Xnor:
    return ReductionKind::Xor;
  }
  llvm_unreachable("unknown reduction kind");
}

/// Logical negation of a one-bit 0/1/X value inverts it and keeps X
/// (11.4.7). Comparisons and reductions produce such values, so negating them
/// selects the inverse operator (11.4.4-6, 11.4.9). A one-bit bitwise
/// negation is the same operator (Table 11-15).
struct SimplifyNegation final : OpRewritePattern<SimLogicUnaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicUnaryOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    if (op.getKind() == UnaryKind::LogicalNot &&
        op.getInput().getType().getWidth() == 1) {
      replaceWithNewOp(rewriter, op,
                       createNegation(rewriter, location, op.getInput()));
      return success();
    }
    if (op.getKind() == UnaryKind::Negate) {
      // An arithmetic value is fully known or entirely X, and negation maps
      // each of those to itself after two applications (11.4.3).
      auto inner = op.getInput().getDefiningOp<SimLogicUnaryOp>();
      if (inner && inner.getKind() == UnaryKind::Negate &&
          isWholeValueArithmetic(inner.getInput())) {
        rewriter.replaceOp(op, inner.getInput());
        return success();
      }
      return failure();
    }
    if (op.getKind() == UnaryKind::BitNot && op.getType().getWidth() != 1) {
      // ~~x is x unless x holds Z, which the inner negation turns into X.
      auto inner = op.getInput().getDefiningOp<SimLogicUnaryOp>();
      if (inner && inner.getKind() == UnaryKind::BitNot &&
          cannotHoldHighImpedance(inner.getInput())) {
        rewriter.replaceOp(op, inner.getInput());
        return success();
      }
      return failure();
    }
    if (!isTruthNegation(op))
      return failure();
    Operation *producer = op.getInput().getDefiningOp();
    if (!producer)
      return failure();
    // Replication, extension and the OR reduction keep the truth value of
    // their source (see SimplifyTruthSource).
    if (op.getKind() == UnaryKind::LogicalNot) {
      Value source = stripTruthPreservingProducers(op.getInput());
      if (source != op.getInput()) {
        replaceWithNewOp(rewriter, op,
                         createNegation(rewriter, location, source));
        return success();
      }
    }
    if (auto compare = dyn_cast<SimLogicCompareOp>(producer)) {
      std::optional<CompareKind> inverse =
          getInverseComparison(compare.getKind());
      if (!inverse)
        return failure();
      replaceWithNewOp(
          rewriter, op,
          SimLogicCompareOp::create(rewriter, location, op.getType(), *inverse,
                                    compare.getLhs(), compare.getRhs()));
      return success();
    }
    if (auto reduction = dyn_cast<SimLogicReductionOp>(producer)) {
      replaceWithNewOp(
          rewriter, op,
          SimLogicReductionOp::create(rewriter, location, op.getType(),
                                      getInverseReduction(reduction.getKind()),
                                      reduction.getInput()));
      return success();
    }
    if (auto negation = dyn_cast<SimLogicUnaryOp>(producer);
        negation && isTruthNegation(negation)) {
      // !!x is the truth value of x.
      replaceWithNewOp(
          rewriter, op,
          SimLogicReductionOp::create(rewriter, location, op.getType(),
                                      ReductionKind::Or, negation.getInput()));
      return success();
    }
    return failure();
  }
};

/// 11.4.9: ~|x is !x. A one-bit reduction applies its table to a single bit:
/// AND, OR and XOR all map Z to X and keep 0, 1 and X, which is the identity
/// on a value that cannot hold Z; the inverted reductions negate it.
struct SimplifyReductionForm final : OpRewritePattern<SimLogicReductionOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicReductionOp op,
                                PatternRewriter &rewriter) const override {
    TypedValue<LogicType> input = op.getInput();
    bool single = input.getType().getWidth() == 1;
    bool inverted = op.getKind() == ReductionKind::Nand ||
                    op.getKind() == ReductionKind::Nor ||
                    op.getKind() == ReductionKind::Xnor;
    if (op.getKind() == ReductionKind::Nor || (single && inverted)) {
      replaceWithNewOp(rewriter, op,
                       createNegation(rewriter, op.getLoc(), input));
      return success();
    }
    if (!single)
      return failure();
    if (cannotHoldHighImpedance(input)) {
      rewriter.replaceOp(op, input);
      return success();
    }
    if (op.getKind() == ReductionKind::Or)
      return failure();
    rewriter.modifyOpInPlace(op, [&] { op.setKind(ReductionKind::Or); });
    return success();
  }
};

/// x == 0 is !x and x != 0 is the truth value of x: both are X exactly when
/// x has no known one bit but some X or Z bit (11.4.5, 11.4.7).
struct CompareWithZero final : OpRewritePattern<SimLogicCompareOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicCompareOp op,
                                PatternRewriter &rewriter) const override {
    if (op.getKind() != CompareKind::Eq && op.getKind() != CompareKind::Ne)
      return failure();
    Value value;
    if (isKnownZero(op.getRhs()))
      value = op.getLhs();
    else if (isKnownZero(op.getLhs()))
      value = op.getRhs();
    else
      return failure();
    if (op.getKind() == CompareKind::Eq)
      replaceWithNewOp(rewriter, op,
                       createNegation(rewriter, op.getLoc(), value));
    else
      replaceWithNewOp(rewriter, op,
                       SimLogicReductionOp::create(rewriter, op.getLoc(),
                                                   op.getType(),
                                                   ReductionKind::Or, value));
    return success();
  }
};

/// Table 11-20 is symmetric in the two arms, and an X or Z condition stays
/// ambiguous under negation, so a negated condition swaps the arms. With
/// constant one-bit arms the conditional operator is a truth test.
struct SimplifyMuxCondition final : OpRewritePattern<SimLogicMuxOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicMuxOp op,
                                PatternRewriter &rewriter) const override {
    Location location = op.getLoc();
    if (auto negation = op.getCondition().getDefiningOp<SimLogicUnaryOp>();
        negation && isTruthNegation(negation)) {
      Value condition = negation.getInput();
      if (condition.getType() != op.getCondition().getType())
        condition = SimLogicReductionOp::create(rewriter, location,
                                                op.getCondition().getType(),
                                                ReductionKind::Or, condition);
      replaceWithNewOp(rewriter, op,
                       SimLogicMuxOp::create(rewriter, location, op.getType(),
                                             condition, op.getFalseValue(),
                                             op.getTrueValue()));
      return success();
    }
    // c ? 1'b1 : 1'b0 is c with Z mapped to X, and c ? 1'b0 : 1'b1 is !c.
    if (op.getType().getWidth() == 1) {
      if (isKnownOne(op.getTrueValue()) && isKnownZero(op.getFalseValue())) {
        replaceWithNewOp(
            rewriter, op,
            SimLogicReductionOp::create(rewriter, location, op.getType(),
                                        ReductionKind::Or, op.getCondition()));
        return success();
      }
      if (isKnownZero(op.getTrueValue()) && isKnownOne(op.getFalseValue())) {
        replaceWithNewOp(rewriter, op,
                         createNegation(rewriter, location, op.getCondition()));
        return success();
      }
    }
    // Table 11-20 keeps matching known bits and turns Z into X, so equal arms
    // that cannot hold Z are returned unchanged.
    if (op.getTrueValue() == op.getFalseValue() &&
        cannotHoldHighImpedance(op.getTrueValue())) {
      rewriter.replaceOp(op, op.getTrueValue());
      return success();
    }
    return failure();
  }
};

/// Identities of the bitwise and arithmetic operators that hold in four-state
/// logic. x ^ '1 is ~x and x ~^ '0 is ~x (Table 11-14). The remaining
/// identities normalize Z to X, so they return x only when x cannot hold Z
/// (bitwise) or is a whole-value arithmetic result (11.4.3); a one-bit
/// normalization is the truth value of x.
struct SimplifyBinaryIdentity final : OpRewritePattern<SimLogicBinaryOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicBinaryOp op,
                                PatternRewriter &rewriter) const override {
    Value lhs = op.getLhs();
    Value rhs = op.getRhs();
    Location location = op.getLoc();
    BinaryKind kind = op.getKind();
    if ((kind == BinaryKind::Xor && isKnownAllOnes(rhs)) ||
        (kind == BinaryKind::Xnor && isKnownZero(rhs))) {
      replaceWithNewOp(rewriter, op,
                       SimLogicUnaryOp::create(rewriter, location, op.getType(),
                                               UnaryKind::BitNot, lhs));
      return success();
    }
    bool normalization =
        ((kind == BinaryKind::And || kind == BinaryKind::Or) && lhs == rhs) ||
        (kind == BinaryKind::And && isKnownAllOnes(rhs)) ||
        (kind == BinaryKind::Xnor && isKnownAllOnes(rhs)) ||
        ((kind == BinaryKind::Or || kind == BinaryKind::Xor) &&
         isKnownZero(rhs));
    if (normalization) {
      if (cannotHoldHighImpedance(lhs)) {
        rewriter.replaceOp(op, lhs);
        return success();
      }
      if (op.getType().getWidth() != 1)
        return failure();
      replaceWithNewOp(rewriter, op,
                       SimLogicReductionOp::create(rewriter, location,
                                                   op.getType(),
                                                   ReductionKind::Or, lhs));
      return success();
    }
    bool arithmeticIdentity =
        ((kind == BinaryKind::Add || kind == BinaryKind::Sub) &&
         isKnownZero(rhs)) ||
        ((kind == BinaryKind::Mul || kind == BinaryKind::UDiv ||
          kind == BinaryKind::SDiv) &&
         isKnownOne(rhs));
    if (arithmeticIdentity && isWholeValueArithmetic(lhs)) {
      rewriter.replaceOp(op, lhs);
      return success();
    }
    return failure();
  }
};

//===----------------------------------------------------------------------===//
// Structural selections
//===----------------------------------------------------------------------===//

/// A slice of an extended value is a slice of the source, the extension
/// bits, or both (11.6.1). Sign-extension bits replicate the source MSB,
/// including X and Z.
struct ExtractOfResize final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto resize = op.getInput().getDefiningOp<SimLogicResizeOp>();
    if (!resize)
      return failure();
    TypedValue<LogicType> source = resize.getInput();
    uint64_t sourceWidth = source.getType().getWidth();
    uint64_t low = op.getLowBit();
    uint64_t width = op.getType().getWidth();
    Location location = op.getLoc();
    if (low + width <= sourceWidth) {
      if (low == 0 && width == sourceWidth) {
        rewriter.replaceOp(op, source);
        return success();
      }
      replaceWithNewOp(
          rewriter, op,
          SimLogicExtractOp::create(rewriter, location, op.getType(), source,
                                    rewriter.getI64IntegerAttr(low)));
      return success();
    }
    // The resize can only be widening here, since the slice lies within it.
    if (low >= sourceWidth) {
      if (!resize.getIsSigned()) {
        rewriter.replaceOp(op, createKnownLogic(rewriter, location, width,
                                                APInt::getZero(width)));
        return success();
      }
      Value sign = SimLogicExtractOp::create(
          rewriter, location, LogicType::get(op.getContext(), 1), source,
          rewriter.getI64IntegerAttr(sourceWidth - 1));
      replaceWithNewOp(
          rewriter, op,
          SimLogicReplicateOp::create(rewriter, location, op.getType(), sign,
                                      rewriter.getI64IntegerAttr(width)));
      return success();
    }
    Value part = source;
    if (low != 0)
      part = SimLogicExtractOp::create(
          rewriter, location,
          LogicType::get(op.getContext(), sourceWidth - low), source,
          rewriter.getI64IntegerAttr(low));
    replaceWithNewOp(rewriter, op,
                     SimLogicResizeOp::create(rewriter, location, op.getType(),
                                              part, resize.getIsSigned()));
    return success();
  }
};

/// Bitwise operators and the conditional operator act on each bit position
/// independently (Tables 11-12 through 11-15, Table 11-20), so a slice of
/// their result is the operator applied to slices. Arithmetic is excluded:
/// an X anywhere in an operand makes every result bit X (11.4.3).
struct NarrowBitwiseExtract final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  /// A slice of a constant or of a concatenation, replication, slice,
  /// insertion or resize folds into its source, so narrowing does not add a
  /// selection per operand.
  static bool sliceFolds(Value value) {
    return isConstantValue(value) ||
           isa_and_nonnull<SimLogicConcatOp, SimLogicReplicateOp,
                           SimLogicExtractOp, SimLogicInsertOp,
                           SimLogicResizeOp>(value.getDefiningOp());
  }

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    Operation *producer = op.getInput().getDefiningOp();
    if (!producer || !producer->hasOneUse())
      return failure();
    if (isa<SimLogicBinaryOp>(producer) &&
        !llvm::all_of(producer->getOperands(), sliceFolds))
      return failure();
    if (auto mux = dyn_cast<SimLogicMuxOp>(producer);
        mux &&
        (!sliceFolds(mux.getTrueValue()) || !sliceFolds(mux.getFalseValue())))
      return failure();
    Location location = op.getLoc();
    auto slice = [&](Value value) -> Value {
      return SimLogicExtractOp::create(rewriter, location, op.getType(), value,
                                       op.getLowBitAttr());
    };
    Value replacement;
    if (auto binary = dyn_cast<SimLogicBinaryOp>(producer)) {
      switch (binary.getKind()) {
      case BinaryKind::And:
      case BinaryKind::Or:
      case BinaryKind::Xor:
      case BinaryKind::Xnor:
        replacement = SimLogicBinaryOp::create(
            rewriter, location, op.getType(), binary.getKind(),
            slice(binary.getLhs()), slice(binary.getRhs()));
        break;
      default:
        break;
      }
    } else if (auto unary = dyn_cast<SimLogicUnaryOp>(producer)) {
      if (unary.getKind() == UnaryKind::BitNot)
        replacement =
            SimLogicUnaryOp::create(rewriter, location, op.getType(),
                                    UnaryKind::BitNot, slice(unary.getInput()));
    } else if (auto mux = dyn_cast<SimLogicMuxOp>(producer)) {
      replacement = SimLogicMuxOp::create(
          rewriter, location, op.getType(), mux.getCondition(),
          slice(mux.getTrueValue()), slice(mux.getFalseValue()));
    }
    if (!replacement)
      return failure();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

/// A slice of a value shifted by a constant is a slice of the source, the
/// vacated fill, or both (11.4.10). Logical shifts fill with zero and an
/// arithmetic right shift with the source MSB, including X and Z.
struct ExtractOfConstantShift final : OpRewritePattern<SimLogicExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto shift = op.getInput().getDefiningOp<SimLogicShiftOp>();
    if (!shift)
      return failure();
    std::optional<ConstantIndex> amount = getConstantIndex(shift.getAmount());
    uint64_t sourceWidth = shift.getType().getWidth();
    // Unknown, zero and out-of-range amounts are folded on the shift itself.
    if (!amount || !amount->value || amount->value->isZero() ||
        amount->value->uge(sourceWidth))
      return failure();
    uint64_t k = amount->value->getZExtValue();
    uint64_t low = op.getLowBit();
    uint64_t width = op.getType().getWidth();
    Value source = shift.getInput();
    Location location = op.getLoc();
    MLIRContext *context = op.getContext();
    auto zero = [&](uint64_t bits) {
      return createKnownLogic(rewriter, location, bits, APInt::getZero(bits));
    };
    auto slice = [&](uint64_t from, uint64_t bits) -> Value {
      if (from == 0 && bits == sourceWidth)
        return source;
      return SimLogicExtractOp::create(rewriter, location,
                                       LogicType::get(context, bits), source,
                                       rewriter.getI64IntegerAttr(from));
    };
    Value replacement;
    if (shift.getKind() == ShiftKind::Left) {
      // Result bit i is source bit i - k, or zero below k.
      if (low >= k)
        replacement = slice(low - k, width);
      else if (low + width <= k)
        replacement = zero(width);
      else
        replacement = SimLogicConcatOp::create(
            rewriter, location, op.getType(),
            ValueRange{slice(0, low + width - k), zero(k - low)});
    } else {
      // Result bit i is source bit i + k, or the fill above sourceWidth - k.
      bool arithmetic = shift.getKind() == ShiftKind::RightArith;
      if (low + width + k <= sourceWidth)
        replacement = slice(low + k, width);
      else if (low + k >= sourceWidth && !arithmetic)
        replacement = zero(width);
      else if (low + k >= sourceWidth)
        replacement = SimLogicReplicateOp::create(
            rewriter, location, op.getType(), slice(sourceWidth - 1, 1),
            rewriter.getI64IntegerAttr(width));
      else
        replacement =
            SimLogicResizeOp::create(rewriter, location, op.getType(),
                                     slice(low + k, sourceWidth - low - k),
                                     rewriter.getBoolAttr(arithmetic));
    }
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

/// A constant selection that overlaps the input only partially reads X for
/// its out-of-range bits and writes only its in-range bits (11.5.1).
struct PartialConstantDynamicExtract final
    : OpRewritePattern<SimLogicDynExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicDynExtractOp op,
                                PatternRewriter &rewriter) const override {
    std::optional<ConstantIndex> index = getConstantIndex(op.getLowBit());
    if (!index || !index->value)
      return failure();
    uint64_t inputWidth = op.getInput().getType().getWidth();
    uint64_t width = op.getType().getWidth();
    const APInt &low = *index->value;
    Location location = op.getLoc();
    MLIRContext *context = op.getContext();
    auto unknown = [&](uint64_t bits) {
      return createLogicConstant(rewriter, location, bits, APInt::getZero(bits),
                                 APInt::getAllOnes(bits));
    };
    SmallVector<Value, 2> pieces;
    if (!low.isNegative()) {
      if (low.uge(inputWidth) || low.getZExtValue() + width <= inputWidth)
        return failure();
      uint64_t start = low.getZExtValue();
      pieces = {unknown(start + width - inputWidth),
                SimLogicExtractOp::create(
                    rewriter, location,
                    LogicType::get(context, inputWidth - start), op.getInput(),
                    rewriter.getI64IntegerAttr(start))};
    } else {
      APInt skipped = -low;
      if (skipped.uge(width))
        return failure();
      uint64_t below = skipped.getZExtValue();
      pieces = {SimLogicExtractOp::create(
                    rewriter, location, LogicType::get(context, width - below),
                    op.getInput(), rewriter.getI64IntegerAttr(0)),
                unknown(below)};
    }
    replaceWithNewOp(
        rewriter, op,
        SimLogicConcatOp::create(rewriter, location, op.getType(), pieces));
    return success();
  }
};

struct PartialConstantDynamicInsert final
    : OpRewritePattern<SimLogicDynInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimLogicDynInsertOp op,
                                PatternRewriter &rewriter) const override {
    std::optional<ConstantIndex> index = getConstantIndex(op.getLowBit());
    if (!index || !index->value)
      return failure();
    uint64_t inputWidth = op.getInput().getType().getWidth();
    uint64_t width = op.getReplacement().getType().getWidth();
    const APInt &low = *index->value;
    Location location = op.getLoc();
    MLIRContext *context = op.getContext();
    uint64_t sourceLow;
    uint64_t pieceWidth;
    uint64_t targetLow;
    if (!low.isNegative()) {
      if (low.uge(inputWidth) || low.getZExtValue() + width <= inputWidth)
        return failure();
      targetLow = low.getZExtValue();
      sourceLow = 0;
      pieceWidth = inputWidth - targetLow;
    } else {
      APInt skipped = -low;
      if (skipped.uge(width))
        return failure();
      targetLow = 0;
      sourceLow = skipped.getZExtValue();
      pieceWidth = width - sourceLow;
    }
    Value piece = SimLogicExtractOp::create(
        rewriter, location, LogicType::get(context, pieceWidth),
        op.getReplacement(), rewriter.getI64IntegerAttr(sourceLow));
    replaceWithNewOp(rewriter, op,
                     SimLogicInsertOp::create(
                         rewriter, location, op.getType(), op.getInput(), piece,
                         rewriter.getI64IntegerAttr(targetLow)));
    return success();
  }
};

/// IEEE 1800-2023 7.4.6: writing back the element just read changes nothing.
/// An invalid index reads the default and makes the write a no-op, so this
/// holds for every index.
struct RemoveArrayWriteBack final : OpRewritePattern<SimArrayDynInsertOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimArrayDynInsertOp op,
                                PatternRewriter &rewriter) const override {
    auto extract = op.getReplacement().getDefiningOp<SimArrayDynExtractOp>();
    if (!extract || extract.getInput() != op.getInput() ||
        extract.getIndex() != op.getIndex())
      return failure();
    rewriter.replaceOp(op, op.getInput());
    return success();
  }
};

/// IEEE 1800-2023 7.3.1: members of a hard packed union share all their bits,
/// so reading another member reinterprets them. A two-state member read from
/// four-state bits converts X and Z to zero.
struct ReinterpretPackedUnionMember final
    : OpRewritePattern<SimUnionExtractOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(SimUnionExtractOp op,
                                PatternRewriter &rewriter) const override {
    auto construct = op.getInput().getDefiningOp<SimUnionConstructOp>();
    auto unionType = dyn_cast<PackedUnionType>(op.getInput().getType());
    if (!construct || !unionType || unionType.getIsTagged() ||
        construct.getIndex() == op.getIndex())
      return failure();
    Value value = construct.getValue();
    Type sourceType = value.getType();
    Type resultType = op.getType();
    Type sourceScalar = getPackedScalarType(sourceType);
    Type resultScalar = getPackedScalarType(resultType);
    std::optional<unsigned> unionWidth = getPackedWidth(unionType);
    if (!sourceScalar || !resultScalar || !unionWidth ||
        getPackedWidth(sourceScalar) != unionWidth ||
        getPackedWidth(resultScalar) != unionWidth)
      return failure();
    Location location = op.getLoc();
    if (sourceType != sourceScalar)
      value =
          SimPackedFlattenOp::create(rewriter, location, sourceScalar, value);
    if (isa<LogicType>(sourceScalar) && isa<IntegerType>(resultScalar))
      value = SimLogicToBitsOp::create(rewriter, location, resultScalar, value);
    else if (isa<IntegerType>(sourceScalar) && isa<LogicType>(resultScalar))
      value =
          SimLogicFromBitsOp::create(rewriter, location, resultScalar, value);
    if (resultType != resultScalar)
      value =
          SimPackedUnflattenOp::create(rewriter, location, resultType, value);
    rewriter.replaceOp(op, value);
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

void SimAggregateSplatOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<SimplifyAggregateSplat>(context);
}

void SimAggregateExportBitstreamOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FoldSmallAggregateExportBitstream>(context);
}

void SimRecursiveExportBitstreamOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FoldNullRecursiveExportBitstream>(context);
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
  results.add<ConstantArrayExtract, DynamicArrayExtractThroughReference,
              TwoStateIndex<SimArrayDynExtractOp, 1>>(context);
}

void SimArrayDynInsertOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<ConstantArrayInsert, RemoveArrayWriteBack,
              TwoStateIndex<SimArrayDynInsertOp, 2>>(context);
}

void SimUnionExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyUnionExtract, ReinterpretPackedUnionMember>(context);
}

void SimRefSubelementOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<FlattenSubelementPath<SimRefSubelementOp>, PackedLocalSubelement,
              FourStatePackedMember>(context);
  results.add<PromoteLocalArrayView<SimRefSubelementOp>>(context);
}

void SimRefAllocOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                MLIRContext *context) {
  results.add<ExplicitReenteredLocalInitialization>(context);
}

void SimRefArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateIndex<SimRefArrayElementOp, 1>>(context);
  results.add<ConstantArrayView<SimRefArrayElementOp, SimRefSubelementOp>>(
      context);
  results.add<PromoteLocalArrayView<SimRefArrayElementOp>>(context);
}

void SimDriverSubelementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FlattenSubelementPath<SimDriverSubelementOp>>(context);
}

void SimDriverArrayElementOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateIndex<SimDriverArrayElementOp, 1>>(context);
  results
      .add<ConstantArrayView<SimDriverArrayElementOp, SimDriverSubelementOp>>(
          context);
}

void SimLogicBinaryOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<NormalizeBinaryConstant, TwoStateBinary, CombineBinaryConstants,
              SimplifyBinaryIdentity>(context);
}

void SimLogicUnaryOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<TwoStateUnary, SimplifyNegation>(context);
}

void SimLogicReductionOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateReduction, TwoStateParity, ReduceReplication,
              SimplifyReductionForm>(context);
}

void SimLogicLogicalOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyLogical, SingleBitLogicalToBitwise>(context);
}

void SimLogicMuxOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                MLIRContext *context) {
  results.add<TwoStateMux, SimplifyMuxCondition>(context);
}

void SimLogicCaseDifferenceMaskOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateDifferenceMask>(context);
}

void SimLogicShiftOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<TwoStateIndex<SimLogicShiftOp, 1>, TwoStateShift>(context);
}

void SimStringCompareOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<FoldStringCompare>(context);
}

void SimStringLengthOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<FoldStringLength>(context);
}

void SimStringGetcOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<FoldStringGetc>(context);
}

void SimStringToPackedOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FoldStringToPacked>(context);
}

void SimStringToPackedExactOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<FoldExactStringToPacked>(context);
}

void SimLogicCompareOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<NormalizeCompareConstant, TwoStateCompare, CompareWithZero>(
      context);
}

void SimLogicResizeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<CollapseResizeChain, TwoStateResize, TruncatingResizeToExtract>(
      context);
}

void SimLogicIsTrueOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<TwoStateTruth, SimplifyTruthSource, SimplifyTruthCondition>(
      context);
}

void SimLogicToBitsOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<ConvertResizedLogicToBits, ConvertLogicToBitsThroughProducer>(
      context);
}

void SimLogicCountBitsOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<DeduplicateCountControls, CountBitsWithPopulationCount>(context);
}

void SimLogicConcatOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<FlattenConcat, ReplicateRepeatedConcatInput,
              MergeAdjacentConcatExtracts, MergeConcatConstants,
              ConcatLeadingZerosToResize>(context);
}

void SimLogicReplicateOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<CombineReplication>(context);
}

void SimLogicExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                    MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimLogicExtractOp>,
              SimplifyLogicExtractSource, ExtractOfResize, NarrowBitwiseExtract,
              ExtractOfConstantShift, TwoStateExtract>(context);
}

void SimLogicDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateIndex<SimLogicDynExtractOp, 1>>(context);
  results.add<ConstantDynamicExtract<SimLogicDynExtractOp, SimLogicExtractOp>,
              PartialConstantDynamicExtract>(context);
}

void SimLogicDynInsertOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateIndex<SimLogicDynInsertOp, 2>>(context);
  results.add<ConstantDynamicInsert<SimLogicDynInsertOp, SimLogicInsertOp>,
              PartialConstantDynamicInsert, TwoStateDynamicInsert>(context);
}

void SimLogicInsertOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                   MLIRContext *context) {
  results.add<RemoveOverwrittenInsert>(context);
}

void SimBitsDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results
      .add<TwoStateIndex<SimBitsDynExtractOp, 1>, SimplifyBitsExtractSource,
           ConstantBitsDynamicExtract>(context);
}

void SimBitsDynInsertOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<TwoStateIndex<SimBitsDynInsertOp, 2>>(context);
}

void SimRefExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimRefExtractOp>,
              PromotePackedLocalView<SimRefExtractOp>>(context);
}

void SimNetExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                  MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimNetExtractOp>>(context);
}

void SimRefDynExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<TwoStateIndex<SimRefDynExtractOp, 1>>(context);
  results.add<ConstantDynamicExtract<SimRefDynExtractOp, SimRefExtractOp>>(
      context);
  results.add<PromotePackedLocalView<SimRefDynExtractOp>>(context);
}

void SimDriverExtractOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                                     MLIRContext *context) {
  results.add<SimplifyStaticExtract<SimDriverExtractOp>>(context);
}

void SimDriverDynExtractOp::getCanonicalizationPatterns(
    RewritePatternSet &results, MLIRContext *context) {
  results.add<TwoStateIndex<SimDriverDynExtractOp, 1>>(context);
  results
      .add<ConstantDynamicExtract<SimDriverDynExtractOp, SimDriverExtractOp>>(
          context);
}

void ObeliskSimulationDialect::getCanonicalizationPatterns(
    RewritePatternSet &results) const {
  results.add<ExtractBitsInsertWithTruncation>(getContext());
}

} // namespace obelisk::sim
