//===- ObeliskDialect.cpp - Obelisk simulation dialect -------------------===//

#include "obelisk/Dialect/Sim/ObeliskOps.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/TypeUtilities.h"

#include "llvm/ADT/ArrayRef.h"

using namespace mlir;

#include "obelisk/Dialect/Sim/ObeliskDialect.cpp.inc"
#include "obelisk/Dialect/Sim/ObeliskEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"

namespace obelisk::ir {

void ObeliskDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"
      >();
}

LogicalResult
LogicType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  unsigned width) {
  if (width == 0)
    return emitError() << "logic width must be greater than zero";
  return success();
}

static LogicalResult requireSameType(Operation *op, Type lhs, Type rhs,
                                     StringRef roles) {
  if (lhs != rhs)
    return op->emitOpError() << roles << " must have identical types (got "
                             << lhs << " and " << rhs << ")";
  return success();
}

LogicalResult LogicConstantOp::verify() {
  auto type = cast<LogicType>(getResult().getType());
  if (getValueAttr().getValue().getBitWidth() != type.getWidth())
    return emitOpError() << "value attribute width must match result width "
                         << type.getWidth();
  if (getUnknownAttr().getValue().getBitWidth() != type.getWidth())
    return emitOpError() << "unknown attribute width must match result width "
                         << type.getWidth();
  return success();
}

LogicalResult LogicConcatOp::verify() {
  if (getInputs().empty())
    return emitOpError("requires at least one input");
  uint64_t totalWidth = 0;
  for (Value input : getInputs())
    totalWidth += cast<LogicType>(input.getType()).getWidth();
  auto resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  if (totalWidth != resultWidth)
    return emitOpError() << "input widths sum to " << totalWidth
                         << " but result width is " << resultWidth;
  return success();
}

LogicalResult LogicExtractOp::verify() {
  auto inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  auto resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  uint64_t lowBit = getLowBit();
  if (lowBit > inputWidth || resultWidth > inputWidth - lowBit)
    return emitOpError() << "part-select [" << (lowBit + resultWidth - 1) << ":"
                         << lowBit << "] exceeds input width " << inputWidth;
  return success();
}

LogicalResult LogicInsertOp::verify() {
  if (failed(requireSameType(*this, getInput().getType(), getResult().getType(),
                             "input and result")))
    return failure();
  auto inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  auto replacementWidth =
      cast<LogicType>(getReplacement().getType()).getWidth();
  uint64_t lowBit = getLowBit();
  if (lowBit > inputWidth || replacementWidth > inputWidth - lowBit)
    return emitOpError() << "replacement at bit " << lowBit
                         << " exceeds input width " << inputWidth;
  return success();
}

LogicalResult LogicFromBitsOp::verify() {
  unsigned inputWidth = getInput().getType().getIntOrFloatBitWidth();
  unsigned resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  if (inputWidth != resultWidth)
    return emitOpError() << "input width " << inputWidth
                         << " does not match result width " << resultWidth;
  return success();
}

LogicalResult LogicToBitsOp::verify() {
  unsigned inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  unsigned resultWidth = getResult().getType().getIntOrFloatBitWidth();
  if (inputWidth != resultWidth)
    return emitOpError() << "input width " << inputWidth
                         << " does not match result width " << resultWidth;
  return success();
}

} // namespace obelisk::ir
