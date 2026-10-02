#include "obelisk/Analysis/DemandedBitAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
using namespace mlir;
namespace obelisk::analysis {
namespace {
unsigned bitWidth(Value value) {
  auto width = sim::getPackedWidth(value.getType());
  return width && *width <= 4096 ? *width : 1;
}
struct Demand {
  std::optional<APInt> bits;
  bool operator==(const Demand &rhs) const { return bits == rhs.bits; }
  static Demand join(const Demand &lhs, const Demand &rhs) {
    if (!lhs.bits)
      return rhs;
    if (!rhs.bits)
      return lhs;
    return {{*lhs.bits | *rhs.bits}};
  }
  static Demand meet(const Demand &lhs, const Demand &rhs) {
    return join(lhs, rhs);
  }
  void print(raw_ostream &os) const {
    if (bits)
      os << *bits;
    else
      os << "bottom";
  }
};
using DemandLattice = dataflow::Lattice<Demand>;
class SparseDemand final
    : public dataflow::SparseBackwardDataFlowAnalysis<DemandLattice> {
public:
  using SparseBackwardDataFlowAnalysis::SparseBackwardDataFlowAnalysis;
  LogicalResult
  visitOperation(Operation *op, ArrayRef<DemandLattice *> operands,
                 ArrayRef<const DemandLattice *> results) override {
    auto full = [&] {
      for (auto *state : operands)
        setToExitState(state);
      return success();
    };
    if (!isMemoryEffectFree(op) || op->getNumRegions())
      return full();
    // Only explicit bitwise operations have a partial demand transfer. All
    // other users conservatively retain their full four-state semantics.
    bool supported = isa<sim::SimLogicExtractOp, sim::SimLogicConcatOp,
                         sim::SimLogicInsertOp, sim::SimLogicResizeOp,
                         sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op);
    if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op))
      supported = binary.getKind() == sim::BinaryKind::And ||
                  binary.getKind() == sim::BinaryKind::Or ||
                  binary.getKind() == sim::BinaryKind::Xor ||
                  binary.getKind() == sim::BinaryKind::Xnor;
    if (auto unary = dyn_cast<sim::SimLogicUnaryOp>(op))
      supported = unary.getKind() == sim::UnaryKind::Plus ||
                  unary.getKind() == sim::UnaryKind::BitNot;
    if (!supported || results.size() != 1)
      return full();
    if (!results.front()->getValue().bits)
      return success();
    const APInt &mask = *results.front()->getValue().bits;
    auto put = [&](unsigned index, const APInt &bits) {
      if (bits.getBitWidth() != bitWidth(op->getOperand(index)))
        return setToExitState(operands[index]);
      propagateIfChanged(operands[index],
                         operands[index]->meet(Demand{{bits}}));
    };
    if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op)) {
      APInt input(bitWidth(extract.getInput()), 0);
      if (mask.getBitWidth() > input.getBitWidth() ||
          extract.getLowBit() > input.getBitWidth() - mask.getBitWidth())
        return full();
      input.insertBits(mask, extract.getLowBit());
      put(0, input);
    } else if (isa<sim::SimLogicConcatOp>(op)) {
      unsigned high = mask.getBitWidth();
      for (auto [index, value] : llvm::enumerate(op->getOperands())) {
        unsigned width = bitWidth(value);
        if (width > high)
          return full();
        high -= width;
        put(index, mask.extractBits(width, high));
      }
    } else if (auto insert = dyn_cast<sim::SimLogicInsertOp>(op)) {
      unsigned width = bitWidth(insert.getReplacement()),
               low = insert.getLowBit();
      if (width > mask.getBitWidth() || low > mask.getBitWidth() - width)
        return full();
      APInt input = mask;
      input.clearBits(low, low + width);
      put(0, input);
      put(1, mask.extractBits(width, low));
    } else if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
      unsigned width = bitWidth(resize.getInput());
      APInt input = mask.zextOrTrunc(width);
      if (resize.getIsSigned() && width < mask.getBitWidth() &&
          !mask.lshr(width).isZero())
        input.setBit(width - 1);
      put(0, input);
    } else {
      for (unsigned index = 0; index < operands.size(); ++index)
        put(index, mask);
    }
    return success();
  }

private:
  void setToExitState(DemandLattice *state) override {
    propagateIfChanged(
        state,
        state->meet(Demand{{APInt::getAllOnes(bitWidth(state->getAnchor()))}}));
  }
  void visitBranchOperand(OpOperand &operand) override {
    setToExitState(getLatticeElement(operand.get()));
  }
  void visitCallOperand(OpOperand &operand) override {
    setToExitState(getLatticeElement(operand.get()));
  }
  void
  visitNonControlFlowArguments(RegionSuccessor &,
                               ArrayRef<BlockArgument> arguments) override {
    for (Value value : arguments)
      setToExitState(getLatticeElement(value));
  }
};
} // namespace
DemandedBitAnalysis::DemandedBitAnalysis(Operation *function) {
  SymbolTableCollection symbols;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<SparseDemand>(symbols);
  if (failed(solver.initializeAndRun(function)))
    return;
  function->walk([&](Operation *op) {
    for (Value result : op->getResults())
      if (auto *state = solver.lookupState<DemandLattice>(result))
        if (state->getValue().bits)
          demands.try_emplace(result, *state->getValue().bits);
  });
}
} // namespace obelisk::analysis
