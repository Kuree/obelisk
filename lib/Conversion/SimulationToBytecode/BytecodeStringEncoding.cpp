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
  if (auto op = dyn_cast<sim::SimStringToPackedExactOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringToPacked, {op.getInput()},
                         {op.getResult(), op.getMatched()}, 1);
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
    uint32_t width = emitU64Constant(plan, op.getWidth());
    if (prefix == kInvalidRegister || specifier == kInvalidRegister ||
        width == kInvalidRegister)
      return op.emitOpError("cannot allocate scan-field operand registers");
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringScanField,
        {reg(plan, op.getInput()), reg(plan, op.getCursor()), prefix, specifier,
         width},
        {reg(plan, op.getField()), reg(plan, op.getNextCursor()),
         reg(plan, op.getOk())});
  }
  if (auto op = dyn_cast<sim::SimScanDynamicValidateOp>(operation)) {
    requiresDynamicScanFeature = true;
    uint32_t file = emitU64Constant(plan, op.getFile() ? 1 : 0);
    uint32_t finalize = emitU64Constant(plan, op.getFinalize() ? 1 : 0);
    uint32_t allowed = emitU64Constant(plan, op.getAllowedSpecifiers());
    if (file == kInvalidRegister || finalize == kInvalidRegister ||
        allowed == kInvalidRegister)
      return op.emitOpError("cannot allocate dynamic scan validation operands");
    return emitIntrinsicRegisters(plan, kIntrinsicScanDynamicValidate,
                                  {reg(plan, op.getFormat()),
                                   reg(plan, op.getPlanCursor()), file,
                                   finalize, allowed},
                                  {reg(plan, op.getNextPlanCursor())});
  }
  if (auto op = dyn_cast<sim::SimStringScanDynamicOp>(operation)) {
    requiresDynamicScanFeature = true;
    uint32_t finalize = emitU64Constant(plan, op.getFinalize() ? 1 : 0);
    uint32_t allowed = emitU64Constant(plan, op.getAllowedSpecifiers());
    uint32_t rawTwoState = emitU64Constant(plan, op.getRawTwoStateBytes());
    uint32_t rawFourState = emitU64Constant(plan, op.getRawFourStateBytes());
    if (finalize == kInvalidRegister || allowed == kInvalidRegister ||
        rawTwoState == kInvalidRegister || rawFourState == kInvalidRegister)
      return op.emitOpError("cannot allocate dynamic scan operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringScanDynamic,
        {reg(plan, op.getInput()), reg(plan, op.getCursor()),
         reg(plan, op.getFormat()), reg(plan, op.getPlanCursor()),
         reg(plan, op.getEnabled()), finalize, allowed, rawTwoState,
         rawFourState},
        {reg(plan, op.getField()), reg(plan, op.getNextCursor()),
         reg(plan, op.getNextPlanCursor()), reg(plan, op.getConversionKind()),
         reg(plan, op.getOk())});
  }
  if (auto op = dyn_cast<sim::SimStringScanRawOp>(operation)) {
    uint64_t bitWidth = cast<sim::LogicType>(op.getData().getType()).getWidth();
    uint64_t rawSize = ((bitWidth + 31) / 32) *
                       (op.getFourState() ? uint64_t{8} : uint64_t{4});
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t size = emitU64Constant(plan, rawSize);
    uint32_t width = emitU64Constant(plan, bitWidth);
    uint32_t fourState = emitU64Constant(plan, op.getFourState() ? 1 : 0);
    uint32_t maxWidth = emitU64Constant(plan, op.getMaxWidth());
    if (prefix == kInvalidRegister || size == kInvalidRegister ||
        width == kInvalidRegister || fourState == kInvalidRegister ||
        maxWidth == kInvalidRegister)
      return op.emitOpError("cannot allocate raw-scan operand registers");
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringScanRaw,
        {reg(plan, op.getInput()), reg(plan, op.getCursor()), prefix, size,
         width, fourState, maxWidth},
        {reg(plan, op.getData()), reg(plan, op.getNextCursor()),
         reg(plan, op.getOk())});
  }
  if (auto op = dyn_cast<sim::SimStringSkipRawOp>(operation)) {
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t size = emitU64Constant(plan, op.getByteCount());
    uint32_t zero = emitU64Constant(plan, 0);
    if (prefix == kInvalidRegister || size == kInvalidRegister ||
        zero == kInvalidRegister)
      return op.emitOpError("cannot allocate raw-skip operand registers");
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringScanRaw,
        {reg(plan, op.getInput()), reg(plan, op.getCursor()), prefix, size,
         zero, zero, size},
        {reg(plan, op.getNextCursor()), reg(plan, op.getOk())});
  }
  if (auto op = dyn_cast<sim::SimFileScanFieldOp>(operation)) {
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t specifier = emitU64Constant(plan, op.getSpecifier());
    uint32_t width = emitU64Constant(plan, op.getWidth());
    if (prefix == kInvalidRegister || specifier == kInvalidRegister ||
        width == kInvalidRegister)
      return op.emitOpError("cannot allocate file scan-field operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicFileScanField,
        {reg(plan, op.getDescriptor()), reg(plan, op.getEnabled()), prefix,
         specifier, width},
        {reg(plan, op.getField()), reg(plan, op.getOk()),
         reg(plan, op.getEof())});
  }
  if (auto op = dyn_cast<sim::SimFileScanDynamicOp>(operation)) {
    requiresDynamicScanFeature = true;
    uint32_t finalize = emitU64Constant(plan, op.getFinalize() ? 1 : 0);
    uint32_t allowed = emitU64Constant(plan, op.getAllowedSpecifiers());
    uint32_t rawTwoState = emitU64Constant(plan, op.getRawTwoStateBytes());
    uint32_t rawFourState = emitU64Constant(plan, op.getRawFourStateBytes());
    if (finalize == kInvalidRegister || allowed == kInvalidRegister ||
        rawTwoState == kInvalidRegister || rawFourState == kInvalidRegister)
      return op.emitOpError("cannot allocate dynamic file scan operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicFileScanDynamic,
        {reg(plan, op.getDescriptor()), reg(plan, op.getFormat()),
         reg(plan, op.getPlanCursor()), reg(plan, op.getEnabled()), finalize,
         allowed, rawTwoState, rawFourState},
        {reg(plan, op.getField()), reg(plan, op.getNextPlanCursor()),
         reg(plan, op.getConversionKind()), reg(plan, op.getOk()),
         reg(plan, op.getEof())});
  }
  if (auto op = dyn_cast<sim::SimFileScanRawOp>(operation)) {
    uint64_t bitWidth = cast<sim::LogicType>(op.getData().getType()).getWidth();
    uint64_t rawSize = ((bitWidth + 31) / 32) *
                       (op.getFourState() ? uint64_t{8} : uint64_t{4});
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t size = emitU64Constant(plan, rawSize);
    uint32_t width = emitU64Constant(plan, bitWidth);
    uint32_t fourState = emitU64Constant(plan, op.getFourState() ? 1 : 0);
    uint32_t maxWidth = emitU64Constant(plan, op.getMaxWidth());
    if (prefix == kInvalidRegister || size == kInvalidRegister ||
        width == kInvalidRegister || fourState == kInvalidRegister ||
        maxWidth == kInvalidRegister)
      return op.emitOpError("cannot allocate file raw-scan operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicFileScanRaw,
        {reg(plan, op.getDescriptor()), reg(plan, op.getEnabled()), prefix,
         size, width, fourState, maxWidth},
        {reg(plan, op.getData()), reg(plan, op.getOk()),
         reg(plan, op.getEof())});
  }
  if (auto op = dyn_cast<sim::SimFileSkipRawOp>(operation)) {
    uint32_t prefix = emitBytesConstant(
        plan, {reinterpret_cast<const uint8_t *>(op.getPrefix().data()),
               op.getPrefix().size()});
    uint32_t size = emitU64Constant(plan, op.getByteCount());
    uint32_t zero = emitU64Constant(plan, 0);
    if (prefix == kInvalidRegister || size == kInvalidRegister ||
        zero == kInvalidRegister)
      return op.emitOpError("cannot allocate file raw-skip operands");
    return emitIntrinsicRegisters(
        plan, kIntrinsicFileScanRaw,
        {reg(plan, op.getDescriptor()), reg(plan, op.getEnabled()), prefix,
         size, zero, zero, size},
        {reg(plan, op.getOk()), reg(plan, op.getEof())});
  }
  if (auto op = dyn_cast<sim::SimStringParseIntegerOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicStringParseInteger,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseLogicOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicPlusargParseLogic,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimPlusargParseLogicOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicPlusargParseLogic,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringParseReal, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimPlusargParseRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicPlusargParseReal, {op.getInput()},
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
