//===- BytecodeStringEncoding.cpp - String instruction selection ---------===//

#include "BytecodeEncoder.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeStringOperation(FunctionPlan &plan, Operation *operation) {
  if (auto op = dyn_cast<sim::SimStringLiteralOp>(operation)) {
    StringRef value = op.getValue();
    uint32_t bytes = emitBytesConstant(
        plan, ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(value.data()),
                                value.size()));
    if (bytes == kInvalidRegister)
      return op.emitOpError("cannot allocate literal byte register");
    return emitIntrinsicRegisters(plan, kIntrinsicStringLiteral, {bytes},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringFromPackedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringFromPacked, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringToPackedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringToPacked, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringConcatOp>(operation)) {
    SmallVector<Value> inputs(op.getInputs());
    return emitIntrinsic(plan, kIntrinsicStringConcat, inputs,
                         {op.getResult()});
  }
  if (auto op = dyn_cast<sim::SimStringRepeatOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringRepeat,
                         {op.getInput(), op.getCount()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringLengthOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringLength, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringGetcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringGetc,
                         {op.getInput(), op.getIndex()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringPutcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringPutc,
                         {op.getInput(), op.getIndex(), op.getCharacter()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringSubstrOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringSubstr,
                         {op.getInput(), op.getLeft(), op.getRight()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringCompareOp>(operation)) {
    uint32_t mode = emitU64Constant(plan, op.getCaseInsensitive() ? 1 : 0);
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringCompare,
        {reg(plan, op.getLhs()), reg(plan, op.getRhs()), mode},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringCaseConvertOp>(operation)) {
    uint32_t mode = emitU64Constant(plan, op.getToUpper() ? 1 : 0);
    return emitIntrinsicRegisters(plan, kIntrinsicStringCaseConvert,
                                  {reg(plan, op.getInput()), mode},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringScanFieldOp>(operation)) {
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t specifier = emitU64Constant(plan, op.getSpecifier());
    if (prefix == kInvalidRegister || specifier == kInvalidRegister)
      return op.emitOpError("cannot allocate scan-field operand registers");
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringScanField,
        {reg(plan, op.getInput()), reg(plan, op.getCursor()), prefix,
         specifier},
        {reg(plan, op.getField()), reg(plan, op.getNextCursor()),
         reg(plan, op.getOk())});
  }
  if (auto op = dyn_cast<sim::SimFileScanFieldOp>(operation)) {
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t specifier = emitU64Constant(plan, op.getSpecifier());
    if (prefix == kInvalidRegister || specifier == kInvalidRegister)
      return op.emitOpError("cannot allocate file scan-field operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicFileScanField,
        {reg(plan, op.getDescriptor()), reg(plan, op.getEnabled()), prefix,
         specifier},
        {reg(plan, op.getField()), reg(plan, op.getOk()),
         reg(plan, op.getEof())});
  }
  if (auto op = dyn_cast<sim::SimStringParseIntegerOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicStringParseInteger,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseLogicOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicStringParseLogic,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringParseReal, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringFormatIntegerOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    uint32_t signedMode = emitU64Constant(plan, op.getIsSigned() ? 1 : 0);
    return emitIntrinsicRegisters(plan, kIntrinsicStringFormatInteger,
                                  {reg(plan, op.getInput()), radix, signedMode},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringFormatRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringFormatReal, {op.getInput()},
                         {op.getResult()});
  return std::nullopt;
}

} // namespace obelisk::bytecode
