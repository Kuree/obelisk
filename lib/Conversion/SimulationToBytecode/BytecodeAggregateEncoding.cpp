//===- BytecodeAggregateEncoding.cpp - Aggregate and logic bytecode ------===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/Support/MathExtras.h"

#include <limits>

using namespace mlir;

namespace obelisk::bytecode {
namespace {

std::optional<uint64_t> unionPayloadSpan(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || packed.getTagBits() > *width)
      return std::nullopt;
    return static_cast<uint64_t>(*width - packed.getTagBits());
  }
  if (isa<sim::UnpackedUnionType>(type))
    return sim::getProvenanceSpan(type);
  return std::nullopt;
}

} // namespace

LogicalResult Encoder::encodeLogicBinary(FunctionPlan &plan,
                                         sim::SimLogicBinaryOp op) {
  uint16_t opcode = 0;
  bool invert = false;
  switch (op.getKind()) {
  case sim::BinaryKind::Add:
    opcode = Add;
    break;
  case sim::BinaryKind::Sub:
    opcode = Sub;
    break;
  case sim::BinaryKind::Mul:
    opcode = Mul;
    break;
  case sim::BinaryKind::UDiv:
    opcode = UDiv;
    break;
  case sim::BinaryKind::SDiv:
    opcode = SDiv;
    break;
  case sim::BinaryKind::UMod:
    opcode = URem;
    break;
  case sim::BinaryKind::SMod:
    opcode = SRem;
    break;
  case sim::BinaryKind::And:
    opcode = And;
    break;
  case sim::BinaryKind::Or:
    opcode = Or;
    break;
  case sim::BinaryKind::Xor:
    opcode = Xor;
    break;
  case sim::BinaryKind::Xnor:
    opcode = Xor;
    invert = true;
    break;
  }
  emit({opcode, 0, reg(plan, op.getResult()), reg(plan, op.getLhs()),
        reg(plan, op.getRhs())});
  if (invert)
    emit({Not, 0, reg(plan, op.getResult()), reg(plan, op.getResult())});
  return success();
}

LogicalResult Encoder::encodeLogicCompare(FunctionPlan &plan,
                                          sim::SimLogicCompareOp op) {
  static constexpr uint16_t map[] = {
      OBELISK_RT_DB_CMP_EQ,       OBELISK_RT_DB_CMP_NE,
      OBELISK_RT_DB_CMP_CASE_EQ,  OBELISK_RT_DB_CMP_CASE_NE,
      OBELISK_RT_DB_CMP_ULT,      OBELISK_RT_DB_CMP_ULE,
      OBELISK_RT_DB_CMP_UGT,      OBELISK_RT_DB_CMP_UGE,
      OBELISK_RT_DB_CMP_SLT,      OBELISK_RT_DB_CMP_SLE,
      OBELISK_RT_DB_CMP_SGT,      OBELISK_RT_DB_CMP_SGE,
      OBELISK_RT_DB_CMP_WILD_EQ,  OBELISK_RT_DB_CMP_WILD_NE,
      OBELISK_RT_DB_CMP_CASEZ_EQ, OBELISK_RT_DB_CMP_CASEXZ_EQ};
  unsigned kind = static_cast<unsigned>(op.getKind());
  if (kind >= std::size(map))
    return op.emitOpError("invalid comparison kind");
  emit({Compare, map[kind], reg(plan, op.getResult()), reg(plan, op.getLhs()),
        reg(plan, op.getRhs())});
  return success();
}

LogicalResult Encoder::encodeConcat(FunctionPlan &plan,
                                    sim::SimLogicConcatOp op) {
  if (op.getInputs().empty())
    return op.emitOpError("empty concatenation");
  if (op.getInputs().size() == 1) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInputs()[0])});
    return success();
  }
  Value left = op.getInputs()[0];
  uint32_t leftRegister = reg(plan, left);
  unsigned accumulatedWidth = cast<sim::LogicType>(left.getType()).getWidth();
  for (unsigned index = 1; index < op.getInputs().size(); ++index) {
    Value right = op.getInputs()[index];
    accumulatedWidth += cast<sim::LogicType>(right.getType()).getWidth();
    uint32_t destination;
    if (index + 1 == op.getInputs().size()) {
      destination = reg(plan, op.getResult());
    } else {
      destination = temporaryLike(
          plan, sim::LogicType::get(op.getContext(), accumulatedWidth),
          op.getResult());
      if (destination == kInvalidRegister)
        return failure();
    }
    emit({Concat, 0, destination, leftRegister, reg(plan, right)});
    leftRegister = destination;
  }
  return success();
}

LogicalResult Encoder::encodeReplicate(FunctionPlan &plan,
                                       sim::SimLogicReplicateOp op) {
  uint64_t count = op.getCount();
  if (count == 0)
    return op.emitOpError("zero replication count");
  if (count == 1) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  uint32_t accumulated = reg(plan, op.getInput());
  unsigned inputWidth =
      cast<sim::LogicType>(op.getInput().getType()).getWidth();
  for (uint64_t copy = 1; copy != count; ++copy) {
    uint32_t destination;
    if (copy + 1 == count) {
      destination = reg(plan, op.getResult());
    } else {
      uint64_t width = uint64_t{inputWidth} * (copy + 1);
      if (width > std::numeric_limits<unsigned>::max())
        return op.emitOpError("replication width exceeds bytecode ABI");
      destination = temporaryLike(
          plan,
          sim::LogicType::get(op.getContext(), static_cast<unsigned>(width)),
          op.getResult());
      if (destination == kInvalidRegister)
        return failure();
    }
    emit({Concat, 0, destination, accumulated, reg(plan, op.getInput())});
    accumulated = destination;
  }
  return success();
}

FailureOr<uint32_t> Encoder::encodeArrayOffset(FunctionPlan &plan, Type array,
                                               Value indexValue,
                                               Operation *anchor) {
  int64_t left = 0, right = 0;
  bool packed = false;
  Type element;
  if (auto type = dyn_cast<sim::PackedArrayType>(array)) {
    left = type.getLeft();
    right = type.getRight();
    packed = true;
    element = type.getElementType();
  } else if (auto type = dyn_cast<sim::UnpackedArrayType>(array)) {
    left = type.getLeft();
    right = type.getRight();
    element = type.getElementType();
  } else {
    anchor->emitOpError("dynamic extraction requires a fixed array");
    return failure();
  }
  std::optional<uint64_t> span = sim::getProvenanceSpan(element);
  uint64_t count = sim::getAggregateNumElements(array);
  if (!span || count == 0) {
    anchor->emitOpError("array element has no fixed packed span");
    return failure();
  }

  MLIRContext *context = anchor->getContext();
  Type calculationType = containsLogic(indexValue.getType())
                             ? Type(sim::LogicType::get(context, 64))
                             : Type(IntegerType::get(context, 64));
  FailureOr<Layout> sourceLayout = getLayout(indexValue.getType());
  if (failed(sourceLayout)) {
    anchor->emitOpError("array index has no bytecode layout");
    return failure();
  }
  uint32_t index = temporary(plan, calculationType);
  uint32_t roundTrip = kInvalidRegister;
  uint32_t fits = kInvalidRegister;
  if (sourceLayout->width > 64) {
    roundTrip = temporary(plan, indexValue.getType());
    fits = temporary(plan, IntegerType::get(context, 1));
  }
  uint32_t leftReg = temporary(plan, calculationType);
  uint32_t rightReg = temporary(plan, calculationType);
  uint32_t lower = temporary(plan, IntegerType::get(context, 1));
  uint32_t upper = temporary(plan, IntegerType::get(context, 1));
  uint32_t valid = temporary(plan, IntegerType::get(context, 1));
  uint32_t ordinal = temporary(plan, calculationType);
  uint32_t scalar = temporary(plan, calculationType);
  uint32_t offset = temporary(plan, calculationType);
  uint32_t invalid = temporary(plan, calculationType);
  if (index == kInvalidRegister || leftReg == kInvalidRegister ||
      rightReg == kInvalidRegister || lower == kInvalidRegister ||
      upper == kInvalidRegister || valid == kInvalidRegister ||
      ordinal == kInvalidRegister || scalar == kInvalidRegister ||
      offset == kInvalidRegister || invalid == kInvalidRegister ||
      (sourceLayout->width > 64 &&
       (roundTrip == kInvalidRegister || fits == kInvalidRegister)))
    return failure();
  emit({Extract, OBELISK_RT_DB_EXTRACT_SIGN_EXTEND, index,
        reg(plan, indexValue), kInvalidRegister});
  if (sourceLayout->width > 64) {
    // Narrowing alone would turn an overflowing or high-plane-X index into
    // an apparently valid low 64-bit value. Require the original index to
    // equal a signed round trip through the runtime's i64 coordinate type.
    emit({Extract, OBELISK_RT_DB_EXTRACT_SIGN_EXTEND, roundTrip, index,
          kInvalidRegister});
    emit({Compare, OBELISK_RT_DB_CMP_EQ, fits, reg(plan, indexValue),
          roundTrip});
  }
  auto constant = [&](uint32_t destination, const APInt &value) {
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addConstant(plan.layouts[destination], value)});
  };
  constant(leftReg, APInt(64, static_cast<uint64_t>(left), true));
  constant(rightReg, APInt(64, static_cast<uint64_t>(right), true));
  if (left >= right) {
    emit({Compare, OBELISK_RT_DB_CMP_SLE, lower, index, leftReg});
    emit({Compare, OBELISK_RT_DB_CMP_SGE, upper, index, rightReg});
    emit({Sub, 0, ordinal, leftReg, index});
  } else {
    emit({Compare, OBELISK_RT_DB_CMP_SGE, lower, index, leftReg});
    emit({Compare, OBELISK_RT_DB_CMP_SLE, upper, index, rightReg});
    emit({Sub, 0, ordinal, index, leftReg});
  }
  emit({And, 0, valid, lower, upper});
  if (sourceLayout->width > 64)
    emit({And, 0, valid, valid, fits});
  if (packed) {
    constant(scalar, APInt(64, count - 1));
    emit({Sub, 0, ordinal, scalar, ordinal});
  }
  constant(scalar, APInt(64, *span));
  emit({Mul, 0, offset, ordinal, scalar});
  constant(invalid, APInt(64, *simulationWidth(array)));
  emit({Select, OBELISK_RT_DB_SELECT_BINARY, offset, offset, invalid, valid});
  return offset;
}

LogicalResult Encoder::encodeArrayExtract(FunctionPlan &plan,
                                          sim::SimArrayDynExtractOp op) {
  FailureOr<uint32_t> offset = encodeArrayOffset(
      plan, op.getInput().getType(), op.getIndex(), op.getOperation());
  if (failed(offset))
    return failure();
  uint32_t destination = reg(plan, op.getResult());
  uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                       ? OBELISK_RT_DB_AGGREGATE_MANAGED
                       : 0;
  emit({Extract, flags, destination, reg(plan, op.getInput()), *offset});
  return success();
}

LogicalResult Encoder::encodeUnionConstruct(FunctionPlan &plan,
                                            sim::SimUnionConstructOp op) {
  Type unionType = op.getResult().getType();
  std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
  std::optional<uint32_t> width = simulationWidth(unionType);
  if (!payloadSpan || !width || *payloadSpan > *width)
    return op.emitOpError("union has no fixed packed representation");
  uint32_t destination = reg(plan, op.getResult());
  uint32_t value = reg(plan, op.getValue());
  uint16_t flags = isManagedAggregateWord(plan.layouts[value].kind)
                       ? OBELISK_RT_DB_AGGREGATE_MANAGED
                       : 0;
  emit({Extract, flags, destination, value, kInvalidRegister});
  uint64_t tag = 0;
  unsigned tagBits = 0;
  if (auto packed = dyn_cast<sim::PackedUnionType>(unionType);
      packed && packed.getIsTagged()) {
    tag = op.getIndex();
    tagBits = packed.getTagBits();
  } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType);
             unpacked && unpacked.getIsTagged()) {
    tag = static_cast<uint64_t>(op.getIndex()) + 1;
    tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
  }
  if (tagBits != 0) {
    uint32_t tagRegister = temporary(plan, unionType);
    if (tagRegister == kInvalidRegister)
      return failure();
    APInt encoded(*width, tag);
    encoded <<= *payloadSpan;
    emit({Constant, 0, tagRegister, 0, 0, 0, 0,
          addConstant(plan.layouts[tagRegister], encoded)});
    emit({Or, 0, destination, destination, tagRegister});
  }
  return success();
}

LogicalResult Encoder::encodeUnionIsActive(FunctionPlan &plan,
                                           sim::SimUnionIsActiveOp op) {
  Type unionType = op.getInput().getType();
  std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
  if (!payloadSpan)
    return op.emitOpError("tagged union has no packed representation");
  unsigned tagBits = 0;
  uint64_t expected = 0;
  if (auto packed = dyn_cast<sim::PackedUnionType>(unionType)) {
    tagBits = packed.getTagBits();
    expected = op.getIndex();
  } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType)) {
    tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
    expected = static_cast<uint64_t>(op.getIndex()) + 1;
  }
  if (tagBits == 0) {
    uint32_t destination = reg(plan, op.getResult());
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addConstant(plan.layouts[destination], APInt(1, 1))});
    return success();
  }
  Type tagType = sim::LogicType::get(op.getContext(), tagBits);
  uint32_t tag = temporaryLike(plan, tagType, op.getInput());
  uint32_t expectedTag = temporaryLike(plan, tagType, op.getInput());
  if (tag == kInvalidRegister || expectedTag == kInvalidRegister)
    return failure();
  emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND, tag,
        reg(plan, op.getInput()), kInvalidRegister, 0, 0, *payloadSpan});
  emit({Constant, 0, expectedTag, 0, 0, 0, 0,
        addConstant(plan.layouts[expectedTag], APInt(tagBits, expected))});
  emit({Compare, OBELISK_RT_DB_CMP_CASE_EQ, reg(plan, op.getResult()), tag,
        expectedTag});
  return success();
}

LogicalResult
Encoder::encodeHandle(FunctionPlan &plan, Value result, uint64_t id,
                      const llvm::DenseMap<uint64_t, uint64_t> &offsets,
                      obelisk_rt_descriptor_kind kind) {
  auto found = offsets.find(id);
  if (found == offsets.end())
    return result.getDefiningOp()->emitOpError("unknown state descriptor");
  Type element;
  if (auto reference = dyn_cast<sim::RefType>(result.getType()))
    element = reference.getElementType();
  else if (auto net = dyn_cast<sim::NetType>(result.getType()))
    element = net.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(result.getType()))
    element = driver.getElementType();
  else
    return result.getDefiningOp()->emitOpError("expected a state handle type");
  uint64_t width = *simulationWidth(element);
  emit({MakeHandle, 0, reg(plan, result), kind, static_cast<uint32_t>(width), 0,
        0, found->second});
  return success();
}

LogicalResult Encoder::encodeHandleOffsetRegister(FunctionPlan &plan,
                                                  Value result, Value input,
                                                  uint64_t offset,
                                                  uint32_t dynamic) {
  Type element;
  if (auto reference = dyn_cast<sim::RefType>(result.getType()))
    element = reference.getElementType();
  else if (auto net = dyn_cast<sim::NetType>(result.getType()))
    element = net.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(result.getType()))
    element = driver.getElementType();
  else
    return result.getDefiningOp()->emitOpError(
        "view result is not a reference, net, or driver");
  std::optional<uint32_t> width = simulationWidth(element);
  if (!width)
    return result.getDefiningOp()->emitOpError(
        "view element has no fixed packed width");
  emit({HandleOffset, 0, reg(plan, result), reg(plan, input), dynamic, 0,
        *width, offset});
  return success();
}

LogicalResult Encoder::encodeHandleOffset(FunctionPlan &plan, Value result,
                                          Value input, uint64_t offset,
                                          Value dynamic) {
  return encodeHandleOffsetRegister(plan, result, input, offset,
                                    dynamic ? reg(plan, dynamic)
                                            : kInvalidRegister);
}

LogicalResult Encoder::encodeSubelementView(FunctionPlan &plan, Value result,
                                            Value input,
                                            ArrayRef<int64_t> indices,
                                            Operation *anchor) {
  Type type;
  if (auto reference = dyn_cast<sim::RefType>(input.getType()))
    type = reference.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(input.getType()))
    type = driver.getElementType();
  else
    return anchor->emitOpError("subelement input is not a state view");
  uint64_t offset = 0;
  for (int64_t rawIndex : indices) {
    if (rawIndex < 0 ||
        static_cast<uint64_t>(rawIndex) >= sim::getAggregateNumElements(type))
      return anchor->emitOpError("subelement index is out of range");
    unsigned index = static_cast<unsigned>(rawIndex);
    auto subelement = sim::getAggregateProvenanceSubelement(type, index);
    if (!subelement ||
        subelement->first > std::numeric_limits<uint64_t>::max() - offset)
      return anchor->emitOpError("subelement path overflows packed offset");
    offset += subelement->first;
    type = sim::getAggregateElementType(type, index);
  }
  return encodeHandleOffsetRegister(plan, result, input, offset,
                                    kInvalidRegister);
}

LogicalResult Encoder::encodeArrayView(FunctionPlan &plan, Value result,
                                       Value input, Value index,
                                       Operation *anchor) {
  Type array;
  if (auto reference = dyn_cast<sim::RefType>(input.getType()))
    array = reference.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(input.getType()))
    array = driver.getElementType();
  else
    return anchor->emitOpError("array view input is not a state view");
  FailureOr<uint32_t> offset = encodeArrayOffset(plan, array, index, anchor);
  if (failed(offset))
    return failure();
  return encodeHandleOffsetRegister(plan, result, input, 0, *offset);
}

void Encoder::emitFrameTransfer(FunctionPlan &plan, uint16_t opcode,
                                Value value, uint64_t offset,
                                uint32_t transferSize) {
  uint16_t kind = 0;
  uint32_t width = 0;
  Type type = value.getType();
  if (auto reference = dyn_cast<sim::RefType>(type)) {
    kind = 2;
    width = *simulationWidth(reference.getElementType());
  } else if (auto net = dyn_cast<sim::NetType>(type)) {
    kind = 3;
    width = *simulationWidth(net.getElementType());
  } else if (auto driver = dyn_cast<sim::DriverType>(type)) {
    kind = 4;
    width = *simulationWidth(driver.getElementType());
  } else if (isa<sim::EventType>(type)) {
    kind = 5;
  }
  if (opcode == LoadFrame)
    emit({LoadFrame, kind, reg(plan, value), 0, 0, 0,
          kind == 0 ? transferSize : width, offset});
  else
    emit({StoreFrame, kind, 0, reg(plan, value), 0, 0,
          kind == 0 ? transferSize : width, offset});
}

} // namespace obelisk::bytecode
