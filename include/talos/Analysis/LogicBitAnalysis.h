#ifndef TALOS_ANALYSIS_LOGICBITANALYSIS_H
#define TALOS_ANALYSIS_LOGICBITANALYSIS_H
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>

namespace talos {
/// Unconditional facts about an SSA snapshot. No stored-state, equality, or
/// initialization assumption is made. Masks are compact words, not per-bit
/// analysis nodes. Large values stay conservative under the analysis budget.
struct LogicBitFacts {
  bool initialized = false;
  llvm::APInt zero, one, known;
  LogicBitFacts() : zero(1, 0), one(1, 0), known(1, 0) {}
  explicit LogicBitFacts(unsigned width)
      : initialized(true), zero(width, 0), one(width, 0), known(width, 0) {}
  bool operator==(const LogicBitFacts &other) const;
  static LogicBitFacts join(const LogicBitFacts &lhs, const LogicBitFacts &rhs);
  void print(llvm::raw_ostream &os) const;
};
using LogicBitLattice = mlir::dataflow::Lattice<LogicBitFacts>;

/// The sparse transfer behind LogicBitAnalysis, for solvers that combine bit
/// facts with other lattices. It depends on SparseConstantPropagation and
/// DeadCodeAnalysis being loaded in the same solver.
class SparseLogicBitAnalysis final
    : public mlir::dataflow::SparseForwardDataFlowAnalysis<LogicBitLattice> {
public:
  using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;
  mlir::LogicalResult
  visitOperation(mlir::Operation *op,
                 llvm::ArrayRef<const LogicBitLattice *> operands,
                 llvm::ArrayRef<LogicBitLattice *> results) override;

private:
  void setToEntryState(LogicBitLattice *state) override;
};

/// The signed range of the values `bits` admits once every bit is known
/// (no X/Z). Bits that may be X/Z can take either value.
std::pair<llvm::APInt, llvm::APInt>
getKnownValueSignedRange(const LogicBitFacts &bits);

class LogicBitAnalysis {
public:
  LogicBitAnalysis() = default;
  explicit LogicBitAnalysis(mlir::Operation *operation);
  std::optional<LogicBitFacts> get(mlir::Value value) const;

private:
  llvm::DenseMap<mlir::Value, LogicBitFacts> facts;
};
} // namespace talos
#endif
