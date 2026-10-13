#include "obelisk/Analysis/LogicBitAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk {
bool LogicBitFacts::operator==(const LogicBitFacts &other) const {
  return initialized == other.initialized && zero == other.zero &&
         one == other.one && known == other.known;
}
LogicBitFacts LogicBitFacts::join(const LogicBitFacts &lhs,
                                  const LogicBitFacts &rhs) {
  if (!lhs.initialized)
    return rhs;
  if (!rhs.initialized)
    return lhs;
  assert(lhs.known.getBitWidth() == rhs.known.getBitWidth());
  LogicBitFacts result(lhs.known.getBitWidth());
  result.zero = lhs.zero & rhs.zero;
  result.one = lhs.one & rhs.one;
  result.known = lhs.known & rhs.known;
  return result;
}
void LogicBitFacts::print(llvm::raw_ostream &os) const {
  os << (initialized ? "known=" : "bottom=") << known;
}
std::pair<APInt, APInt> getKnownValueSignedRange(const LogicBitFacts &bits) {
  unsigned width = bits.known.getBitWidth();
  APInt free = ~(bits.zero | bits.one);
  APInt low = bits.one, high = bits.one | free;
  // The sign bit orders the two halves: a free sign bit takes 1 for the
  // minimum and 0 for the maximum.
  if (free[width - 1]) {
    low.setBit(width - 1);
    high.clearBit(width - 1);
  }
  return {low, high};
}

using BitLattice = LogicBitLattice;
LogicalResult
SparseLogicBitAnalysis::visitOperation(Operation *op,
                                       ArrayRef<const BitLattice *> operands,
                                       ArrayRef<BitLattice *> results) {
  if (results.size() != 1) {
    for (auto *result : results)
      setToEntryState(result);
    return success();
  }
  Value value = op->getResult(0);
  auto type = dyn_cast<sim::LogicType>(value.getType());
  if (!type || type.getWidth() > 4096) {
    setToEntryState(results.front());
    return success();
  }
  // SCCP evaluates dialect fold hooks and forwards constants through region
  // and CFG interfaces. Consume that shared lattice instead of recognizing
  // only literal-producing opcodes here. Keep a dependency until it resolves.
  const auto &constant =
      getOrCreateFor<dataflow::Lattice<dataflow::ConstantValue>>(
          getProgramPointAfter(op), value)
          ->getValue();
  if (constant.isUninitialized())
    return success();
  if (auto planes = dyn_cast_or_null<ArrayAttr>(constant.getConstantValue());
      planes && planes.size() == 2) {
    auto bits = dyn_cast<IntegerAttr>(planes[0]);
    auto unknown = dyn_cast<IntegerAttr>(planes[1]);
    if (bits && unknown && bits.getValue().getBitWidth() == type.getWidth() &&
        unknown.getValue().getBitWidth() == type.getWidth()) {
      LogicBitFacts folded(type.getWidth());
      folded.known = ~unknown.getValue();
      folded.one = bits.getValue() & folded.known;
      folded.zero = ~bits.getValue() & folded.known;
      propagateIfChanged(results.front(), results.front()->join(folded));
      return success();
    }
  }
  // Wait on sparse dependencies; joins on CFG arguments and loops are
  // managed by the solver. No bounded recursive traversal is used.
  for (const auto *operand : operands)
    if (!operand->getValue().initialized)
      return success();
  unsigned width = type.getWidth();
  LogicBitFacts result(width);
  for (Value input : op->getOperands())
    if (auto logic = dyn_cast<sim::LogicType>(input.getType());
        logic && logic.getWidth() > 4096) {
      propagateIfChanged(results.front(), results.front()->join(result));
      return success();
    }
  auto operand = [&](Value input) {
    for (auto [index, actual] : llvm::enumerate(op->getOperands()))
      if (actual == input) {
        const auto &bits = operands[index]->getValue();
        auto logic = dyn_cast<sim::LogicType>(input.getType());
        return logic && bits.known.getBitWidth() == logic.getWidth()
                   ? bits
                   : LogicBitFacts(logic ? logic.getWidth() : width);
      }
    llvm_unreachable("transfer queried a non-operand");
  };
  if (auto from = value.getDefiningOp<sim::SimLogicFromBitsOp>()) {
    result.known.setAllBits();
    APInt bits;
    if (matchPattern(from.getInput(), m_ConstantInt(&bits))) {
      result.one = bits.zextOrTrunc(width);
      result.zero = ~result.one;
    }
  } else if (auto binary = value.getDefiningOp<sim::SimLogicBinaryOp>()) {
    auto lhs = operand(binary.getLhs()), rhs = operand(binary.getRhs());
    switch (binary.getKind()) {
    case sim::BinaryKind::And:
      result.zero = lhs.zero | rhs.zero;
      result.one = lhs.one & rhs.one;
      result.known = (lhs.known & rhs.known) | result.zero;
      break;
    case sim::BinaryKind::Or:
      result.zero = lhs.zero & rhs.zero;
      result.one = lhs.one | rhs.one;
      result.known = (lhs.known & rhs.known) | result.one;
      break;
    case sim::BinaryKind::Xor:
    case sim::BinaryKind::Xnor:
      result.zero = (lhs.zero & rhs.zero) | (lhs.one & rhs.one);
      result.one = (lhs.zero & rhs.one) | (lhs.one & rhs.zero);
      result.known = lhs.known & rhs.known;
      if (binary.getKind() == sim::BinaryKind::Xnor)
        std::swap(result.zero, result.one);
      break;
    default:
      break;
    }
  } else if (auto extract = value.getDefiningOp<sim::SimLogicExtractOp>()) {
    auto input = operand(extract.getInput());
    unsigned low = extract.getLowBit();
    result.zero = input.zero.extractBits(width, low);
    result.one = input.one.extractBits(width, low);
    result.known = input.known.extractBits(width, low);
  } else if (auto concat = value.getDefiningOp<sim::SimLogicConcatOp>()) {
    unsigned low = width;
    for (Value input : concat.getInputs()) {
      auto bits = operand(input);
      low -= bits.known.getBitWidth();
      result.zero.insertBits(bits.zero, low);
      result.one.insertBits(bits.one, low);
      result.known.insertBits(bits.known, low);
    }
  } else if (auto insert = value.getDefiningOp<sim::SimLogicInsertOp>()) {
    result = operand(insert.getInput());
    auto replacement = operand(insert.getReplacement());
    result.zero.insertBits(replacement.zero, insert.getLowBit());
    result.one.insertBits(replacement.one, insert.getLowBit());
    result.known.insertBits(replacement.known, insert.getLowBit());
  } else if (auto resize = value.getDefiningOp<sim::SimLogicResizeOp>()) {
    auto input = operand(resize.getInput());
    auto extend = [&](const APInt &bits) {
      return resize.getIsSigned() ? bits.sextOrTrunc(width)
                                  : bits.zextOrTrunc(width);
    };
    result.zero = extend(input.zero);
    result.one = extend(input.one);
    result.known = extend(input.known);
    if (!resize.getIsSigned() && width > input.known.getBitWidth()) {
      result.zero.setBits(input.known.getBitWidth(), width);
      result.known.setBits(input.known.getBitWidth(), width);
    }
  } else if (auto unary = value.getDefiningOp<sim::SimLogicUnaryOp>()) {
    if (unary.getKind() == sim::UnaryKind::Plus || unary.isBitwiseNegation()) {
      result = operand(unary.getInput());
      if (unary.isBitwiseNegation())
        std::swap(result.zero, result.one);
    }
  } else if (auto select = dyn_cast<SelectLikeOpInterface>(op)) {
    // The predicate is two-state. Preserve facts common to either selected
    // SSA value; unlike logic.mux, selection itself does not merge X/Z bits.
    result = LogicBitFacts::join(operand(select.getTrueValue()),
                                 operand(select.getFalseValue()));
  } else if (auto mux = value.getDefiningOp<sim::SimLogicMuxOp>()) {
    auto condition = operand(mux.getCondition());
    auto yes = operand(mux.getTrueValue()), no = operand(mux.getFalseValue());
    if (condition.one.isAllOnes())
      result = yes;
    else if (condition.zero.isAllOnes())
      result = no;
    else {
      result.zero = yes.zero & no.zero;
      result.one = yes.one & no.one;
      result.known = condition.known.isAllOnes() ? yes.known & no.known
                                                 : result.zero | result.one;
      if (mux.getTrueValue() == mux.getFalseValue())
        result.known |= yes.known;
    }
  } else if (auto reduction = value.getDefiningOp<sim::SimLogicReductionOp>()) {
    auto input = operand(reduction.getInput());
    bool andKind = reduction.getKind() == sim::ReductionKind::And ||
                   reduction.getKind() == sim::ReductionKind::Nand;
    bool orKind = reduction.getKind() == sim::ReductionKind::Or ||
                  reduction.getKind() == sim::ReductionKind::Nor;
    bool invert = reduction.getKind() == sim::ReductionKind::Nand ||
                  reduction.getKind() == sim::ReductionKind::Nor ||
                  reduction.getKind() == sim::ReductionKind::Xnor;
    bool zero =
        andKind ? !input.zero.isZero() : orKind && input.zero.isAllOnes();
    bool one = orKind ? !input.one.isZero() : andKind && input.one.isAllOnes();
    if (!andKind && !orKind && (input.zero | input.one).isAllOnes()) {
      one = (input.one.popcount() & 1) != 0;
      zero = !one;
    }
    result.zero = APInt(1, invert ? one : zero);
    result.one = APInt(1, invert ? zero : one);
    result.known = APInt(1, zero || one || input.known.isAllOnes());
  }
  propagateIfChanged(results.front(), results.front()->join(result));
  return success();
}

void SparseLogicBitAnalysis::setToEntryState(BitLattice *state) {
  auto type = dyn_cast<sim::LogicType>(state->getAnchor().getType());
  unsigned width = type && type.getWidth() <= 4096 ? type.getWidth() : 1;
  propagateIfChanged(state, state->join(LogicBitFacts(width)));
}
LogicBitAnalysis::LogicBitAnalysis(Operation *operation) {
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<SparseLogicBitAnalysis>();
  if (failed(solver.initializeAndRun(operation)))
    return;
  operation->walk([&](Operation *op) {
    for (Value value : op->getResults())
      if (const auto *state = solver.lookupState<BitLattice>(value)) {
        const auto &bits = state->getValue();
        auto logic = dyn_cast<sim::LogicType>(value.getType());
        if (bits.initialized && logic &&
            logic.getWidth() == bits.known.getBitWidth())
          facts.try_emplace(value, bits);
      }
  });
}
std::optional<LogicBitFacts> LogicBitAnalysis::get(Value value) const {
  auto found = facts.find(value);
  return found == facts.end() ? std::optional<LogicBitFacts>{} : found->second;
}
} // namespace obelisk
