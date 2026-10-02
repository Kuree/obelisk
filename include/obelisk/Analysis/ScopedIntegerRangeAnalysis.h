//===- ScopedIntegerRangeAnalysis.h - CFG integer bounds ---------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_SCOPEDINTEGERRANGEANALYSIS_H
#define OBELISK_ANALYSIS_SCOPEDINTEGERRANGEANALYSIS_H
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk::analysis {
/// May ranges at a program point. Missing entries are unconstrained; bottom
/// represents an unvisited edge. CFG joins union ranges, never intersect paths.
class ScopedRangeLattice final : public mlir::dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  using Ranges = llvm::DenseMap<mlir::Value, mlir::ConstantIntRanges>;
  bool reachable = false;
  Ranges ranges;
  mlir::ChangeResult merge(const Ranges &incoming);
  mlir::ChangeResult join(const AbstractDenseLattice &rhs) override;
  void print(llvm::raw_ostream &os) const override;

private:
  unsigned expansions = 0;
};
/// Dense path-sensitive ranges complement the sparse integer-range analysis.
/// Induction limits come from independently validated finite, nonwrapping CFG
/// loops. They include the exit value; continuation edges refine body scopes.
/// Only selectors and their integer dependencies are tracked. Ranges widen at
/// joins to guarantee convergence for other, unbounded loop-carried values.
class ScopedIntegerRangeAnalysis final
    : public mlir::dataflow::DenseForwardDataFlowAnalysis<ScopedRangeLattice> {
public:
  ScopedIntegerRangeAnalysis(mlir::DataFlowSolver &solver,
                             mlir::Operation *function);
  mlir::LogicalResult visitOperation(mlir::Operation *op,
                                     const ScopedRangeLattice &before,
                                     ScopedRangeLattice *after) override;
  void visitBlockTransfer(mlir::Block *block, mlir::ProgramPoint *point,
                          mlir::Block *predecessor,
                          const ScopedRangeLattice &before,
                          ScopedRangeLattice *after) override;
  void visitCallControlFlowTransfer(
      mlir::CallOpInterface call, mlir::dataflow::CallControlFlowAction action,
      const ScopedRangeLattice &before, ScopedRangeLattice *after) override;
  std::optional<mlir::ConstantIntRanges>
  getRange(mlir::Value value, const ScopedRangeLattice &state) const;

private:
  void setToEntryState(ScopedRangeLattice *state) override;
  llvm::DenseSet<mlir::Value> relevant;
  ScopedRangeLattice::Ranges inductionBounds;
  // Exact edge certificates are scoped to this header's continuation edge.
  llvm::DenseMap<mlir::Operation *,
                 std::pair<mlir::Value, mlir::ConstantIntRanges>>
      bodyBounds;
};
} // namespace obelisk::analysis
#endif
