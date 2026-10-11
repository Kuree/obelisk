//===- SSAValueAnalysis.h - Shared forwarding facts -------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_SSAVALUEANALYSIS_H
#define OBELISK_ANALYSIS_SSAVALUEANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk::analysis {

/// Sparse SSA dependence through structured region interfaces. Each result and
/// region argument follows its corresponding successor operands (including
/// scf.yield and scf.condition), rather than all values used inside the region.
/// Predicate/bound dependencies are retained independently of value forwarding.
/// The finite union lattice solves loop-carried recurrences without unrolling.
///
/// Dependencies are relative to the supplied SSA input values. Memory reads
/// must be supplied as inputs; this does not infer reaching memory definitions
/// or authorize reordering effects. Unknown region control flow invalidates the
/// query. Rebuild after changing the IR.
class RegionValueDependencyAnalysis {
public:
  RegionValueDependencyAnalysis(mlir::Operation *root,
                                llvm::ArrayRef<mlir::Value> inputs);
  bool isValid() const { return valid; }
  llvm::ArrayRef<mlir::Value> getInputs(mlir::Value value) const;
  /// Inputs of the operands and enclosing control decisions of an operation.
  llvm::SmallVector<mlir::Value> getInputs(mlir::Operation *operation) const;

private:
  bool valid = false;
  llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> dependencies;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Value>> controls;
};

/// Immutable function snapshots. Rebuild after changing CFG operands.
class SemanticValueRootAnalysis {
public:
  explicit SemanticValueRootAnalysis(sim::SimFuncOp function);
  mlir::Value lookup(mlir::Value value) const {
    auto found = roots.find(value);
    return found == roots.end() ? value : found->second;
  }

private:
  llvm::DenseMap<mlir::Value, mlir::Value> roots;
};

/// Propagate exact forwarded SSA identities in a closed activation. Suspension
/// functions retain their explicit continuation lanes and are left unchanged.
/// CFG arguments remain intact; only dominated uses are replaced.
bool propagateActivationValueRoots(sim::SimFuncOp function);

/// Live-in blocks of individual SSA values, each computed from the value's
/// uses on first query. isLiveIn(value, block) answers exactly what
/// mlir::Liveness(op).getLiveIn(block).contains(value) does for blocks nested
/// in `op`, without building every block's live set. Rebuild after changing
/// the IR.
class ValueLiveness {
public:
  bool isLiveIn(mlir::Value value, mlir::Block *block) {
    return getLiveInBlocks(value).contains(block);
  }
  /// Matches mlir::Liveness's live-out set: the value is live into a
  /// successor, or defined in `block` and used in another block of its region.
  bool isLiveOut(mlir::Value value, mlir::Block *block);
  const llvm::DenseSet<mlir::Block *> &getLiveInBlocks(mlir::Value value);

private:
  llvm::DenseMap<mlir::Value, llvm::DenseSet<mlir::Block *>> liveIns;
};

class ConstantTimeAnalysis {
public:
  explicit ConstantTimeAnalysis(sim::SimFuncOp function);
  bool isConstant(mlir::Value value) const { return constants.contains(value); }

private:
  llvm::DenseSet<mlir::Value> constants;
};

} // namespace obelisk::analysis
#endif
