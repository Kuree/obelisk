#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/NativeWordAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
bool isWordLaneType(Type type) {
  Type scalar = sim::getPackedScalarType(type);
  return scalar && (scalar.isInteger(1) ||
                    scalar == sim::LogicType::get(type.getContext(), 1));
}
bool isWordLaneComputation(Operation *op) {
  if (op->getNumResults() != 1 || !isWordLaneType(op->getResult(0).getType()) ||
      op->getNumRegions() || op->getNumSuccessors() ||
      isa<CallOpInterface>(op) || !isMemoryEffectFree(op))
    return false;
  // Only representation identities and type-agnostic selection folds
  // consume wrapped lanes. Other aggregate folds have different constant
  // attribute encodings and cannot be enumerated as scalar logic planes.
  bool wrapped = isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp,
                     SelectLikeOpInterface>(op);
  if (!wrapped && !isa<IntegerType, sim::LogicType>(op->getResult(0).getType()))
    return false;
  unsigned planes = 0;
  for (Value operand : op->getOperands()) {
    Attribute constant;
    if (matchPattern(operand, m_Constant(&constant)))
      continue;
    if (!isWordLaneType(operand.getType()))
      return false;
    if (!wrapped && !isa<IntegerType, sim::LogicType>(operand.getType()))
      return false;
    planes += isa<sim::LogicType>(sim::getPackedScalarType(operand.getType()))
                  ? 2
                  : 1;
  }
  // Three four-state inputs cover all normalized scalar operators, including
  // ?:; bound enumeration by semantic arity, never by a simulation bit width.
  return planes <= 6;
}
std::optional<WordTruthTable> computeWordTruthTable(Operation *op) {
  if (!isWordLaneComputation(op))
    return std::nullopt;
  WordTruthTable table;
  if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
    // A packed one-bit wrapper changes representation, never its symbol.
    table.operands.push_back(0);
    bool logic = isa<sim::LogicType>(
        sim::getPackedScalarType(op->getResult(0).getType()));
    table.value = APInt(logic ? 4 : 2, logic ? 0xa : 0x2);
    table.unknown = APInt(logic ? 4 : 2, logic ? 0xc : 0x0);
    return table;
  }
  SmallVector<Attribute> inputs;
  unsigned planes = 0;
  for (auto [index, operand] : llvm::enumerate(op->getOperands())) {
    Attribute constant;
    if (!matchPattern(operand, m_Constant(&constant))) {
      table.operands.push_back(index);
      planes += isa<sim::LogicType>(sim::getPackedScalarType(operand.getType()))
                    ? 2
                    : 1;
    }
    inputs.push_back(constant);
  }
  unsigned rows = 1u << planes;
  table.value = APInt(rows, 0);
  table.unknown = APInt(rows, 0);
  // Detach SSA identities/defining ops so fold hooks see independent formal
  // inputs. An x==x shortcut must not replace rows where the inputs differ.
  Block formals;
  Operation *scalar = op->clone();
  formals.push_back(scalar);
  for (OpOperand &operand : scalar->getOpOperands())
    operand.set(formals.addArgument(operand.get().getType(), op->getLoc()));
  auto bit = IntegerType::get(op->getContext(), 1);
  for (unsigned row = 0; row < rows; ++row) {
    unsigned shift = 0;
    for (unsigned index : table.operands) {
      auto value = IntegerAttr::get(bit, (row >> shift++) & 1);
      if (isa<sim::LogicType>(
              sim::getPackedScalarType(op->getOperand(index).getType()))) {
        auto unknown = IntegerAttr::get(bit, (row >> shift++) & 1);
        inputs[index] = ArrayAttr::get(op->getContext(), {value, unknown});
      } else {
        inputs[index] = value;
      }
    }
    SmallVector<OpFoldResult> results;
    if (failed(scalar->fold(inputs, results)) || results.size() != 1)
      return std::nullopt;
    Attribute result;
    if (auto attribute = dyn_cast<Attribute>(results.front()))
      result = attribute;
    else if (auto argument =
                 dyn_cast<BlockArgument>(cast<Value>(results.front())))
      if (argument.getOwner() == &formals)
        result = inputs[argument.getArgNumber()];
    IntegerAttr value, unknown;
    if (isa<sim::LogicType>(
            sim::getPackedScalarType(op->getResult(0).getType()))) {
      auto pair = dyn_cast_or_null<ArrayAttr>(result);
      if (!pair || pair.size() != 2)
        return std::nullopt;
      value = dyn_cast<IntegerAttr>(pair[0]);
      unknown = dyn_cast<IntegerAttr>(pair[1]);
    } else {
      value = dyn_cast_or_null<IntegerAttr>(result);
      unknown = IntegerAttr::get(bit, 0);
    }
    if (!value || !unknown || !value.getType().isInteger(1) ||
        !unknown.getType().isInteger(1))
      return std::nullopt;
    table.value.setBitVal(row, !value.getValue().isZero());
    table.unknown.setBitVal(row, !unknown.getValue().isZero());
  }
  return table;
}
} // namespace obelisk::analysis
