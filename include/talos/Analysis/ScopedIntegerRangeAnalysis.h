//===- ScopedIntegerRangeAnalysis.h - CFG integer bounds ---------*- C++
//-*-===//
#ifndef TALOS_ANALYSIS_SCOPEDINTEGERRANGEANALYSIS_H
#define TALOS_ANALYSIS_SCOPEDINTEGERRANGEANALYSIS_H
#include "talos/Analysis/PersistentIntMap.h"

#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace talos::analysis {
class ScalarEvolutionAnalysis;
/// May ranges at a program point. Missing entries are unconstrained; bottom
/// represents an unvisited edge. CFG joins union ranges, never intersect paths.
class ScopedRangeLattice final : public mlir::dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  /// Ranges keyed by the value's opaque pointer (`keyOf`), in a persistent
  /// map: a transfer copies one path and a join costs only what two states
  /// do not share, where a fused activation holds thousands of facts at
  /// every point.
  using Ranges = PersistentIntMap<mlir::ConstantIntRanges>;
  static uint64_t keyOf(mlir::Value value) {
    return reinterpret_cast<uint64_t>(value.getAsOpaquePointer());
  }
  static mlir::Value valueOf(uint64_t key) {
    return mlir::Value::getFromOpaquePointer(
        reinterpret_cast<const void *>(key));
  }
  bool reachable = false;
  const Ranges &getRanges() const { return ranges; }
  mlir::ChangeResult merge(const Ranges &incoming);
  mlir::ChangeResult join(const AbstractDenseLattice &rhs) override;
  void print(llvm::raw_ostream &os) const override;

private:
  // Identity transfers share immutable facts, not lattice anchors. Each
  // program point must retain its own widening history and dependencies.
  Ranges ranges;
  unsigned expansions = 0;
};
/// Dense path-sensitive ranges complement the sparse integer-range analysis.
/// Induction limits come from independently validated finite, nonwrapping CFG
/// loops. They include the exit value; continuation edges refine body scopes.
/// By default only selectors, calls, returns and their dependencies are
/// tracked. Consumers can request all scalar values for general folding.
/// Ranges widen at joins to converge for unbounded loop-carried values.
class ScopedIntegerRangeAnalysis final
    : public mlir::dataflow::DenseForwardDataFlowAnalysis<ScopedRangeLattice> {
public:
  ScopedIntegerRangeAnalysis(mlir::DataFlowSolver &solver,
                             mlir::Operation *function,
                             bool allScalarValues = false);
  ~ScopedIntegerRangeAnalysis();
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
  bool excludesXZ(mlir::Value value) const;
  bool proveBounds(mlir::Value value, const ScopedRangeLattice &state,
                   const llvm::APInt &lower, const llvm::APInt &upper) const;

private:
  void setToEntryState(ScopedRangeLattice *state) override;
  llvm::DenseSet<mlir::Value> relevant;
  /// (block, value) pairs where a relevant value is live on entry. Block
  /// states only hold relevant values, so this slice of liveness suffices.
  llvm::DenseSet<std::pair<mlir::Block *, mlir::Value>> liveIns;
  std::unique_ptr<ScalarEvolutionAnalysis> evolution;
  llvm::DenseMap<mlir::Value, mlir::ConstantIntRanges> inductionBounds;
  // Exact edge certificates are scoped to this header's continuation edge.
  llvm::DenseMap<mlir::Operation *,
                 std::pair<mlir::Value, mlir::ConstantIntRanges>>
      bodyBounds;
};
} // namespace talos::analysis
#endif
