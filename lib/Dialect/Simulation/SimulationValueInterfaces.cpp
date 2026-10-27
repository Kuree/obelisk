#include "talos/Dialect/Simulation/SimulationOps.h"

#include <cstdint>

using namespace mlir;

namespace talos::sim {
bool SimPackedFlattenOp::areCastCompatible(TypeRange inputs,
                                           TypeRange outputs) {
  return inputs.size() == 1 && outputs.size() == 1 &&
         isAggregateType(inputs.front()) &&
         getPackedScalarType(inputs.front()) == outputs.front();
}

bool SimPackedUnflattenOp::areCastCompatible(TypeRange inputs,
                                             TypeRange outputs) {
  return SimPackedFlattenOp::areCastCompatible(outputs, inputs);
}

bool SimLogicFromBitsOp::areCastCompatible(TypeRange inputs,
                                           TypeRange outputs) {
  if (inputs.size() != 1 || outputs.size() != 1)
    return false;
  auto input = dyn_cast<IntegerType>(inputs.front());
  auto output = dyn_cast<LogicType>(outputs.front());
  return input && input.isSignless() && output &&
         input.getWidth() == output.getWidth();
}

bool SimLogicToBitsOp::areCastCompatible(TypeRange inputs, TypeRange outputs) {
  return SimLogicFromBitsOp::areCastCompatible(outputs, inputs);
}

bool SimLogicResizeOp::areCastCompatible(TypeRange inputs, TypeRange outputs) {
  return inputs.size() == 1 && outputs.size() == 1 &&
         isa<LogicType>(inputs.front()) && isa<LogicType>(outputs.front());
}

void SimLogicCountBitsOp::inferResultRangesFromOptional(
    ArrayRef<IntegerValueRange>, SetIntLatticeFn setResultRange) {
  // The result is a count modulo 2^32. Packed width bounds it independently
  // of which symbols the controls select, including duplicate and X/Z controls.
  // Unpacked storage spans include padding and are not bitstream widths.
  auto width = getPackedWidth(getInput().getType());
  uint32_t maximum = width.value_or(UINT32_MAX);
  setResultRange(getResult(), IntegerValueRange(ConstantIntRanges::fromUnsigned(
                                  APInt(32, 0), APInt(32, maximum))));
}

void SimLogicClog2Op::inferResultRangesFromOptional(
    ArrayRef<IntegerValueRange>, SetIntLatticeFn setResultRange) {
  // X/Z convert to zero first; even signed source values use their unsigned
  // bit pattern. A one-bit input is 0 or 1 and always produces zero.
  unsigned width = getInput().getType().getWidth();
  unsigned maximum = width == 1 ? 0 : width;
  setResultRange(getResult(), IntegerValueRange(ConstantIntRanges::fromUnsigned(
                                  APInt(32, 0), APInt(32, maximum))));
}
} // namespace talos::sim
