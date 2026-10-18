//===- LocalStorageAnalysis.h - Private local storage -----------*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_LOCALSTORAGEANALYSIS_H
#define OBELISK_ANALYSIS_LOCALSTORAGEANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk::analysis {

/// Closed reference families. References cannot
/// escape, carry managed roots, or be observed by event controls. Invocation
/// mode admits a family only when none of its references is live across a
/// suspension, so its storage can use the native invocation's stack
/// (LRM 6.21). Coroutine mode proves privacy, not stack lifetime.
class LocalStorageAnalysis {
public:
  enum class Lifetime { Invocation, Coroutine };
  /// With `passesToCalls`, a family may also be passed to sibling function
  /// calls, which cannot retain a reference (LRM 13.5): for clients that
  /// model what callees write through their formals, not storage lowering.
  explicit LocalStorageAnalysis(sim::SimFuncOp function,
                                Lifetime lifetime = Lifetime::Invocation,
                                bool passesToCalls = false);
  /// Classifies one allocation exactly as a fresh analysis of its function
  /// would, without classifying every other allocation. Forwarding facts are
  /// built only if its reference family crosses a branch.
  static bool isLocal(sim::SimRefAllocOp allocation,
                      Lifetime lifetime = Lifetime::Invocation);
  /// Prove that every read of these recreated allocations follows a complete
  /// definition in this invocation. Dense must-dataflow intersects definitions
  /// across executable CFG and region edges, including loop backedges.
  bool
  areResetBeforeReads(mlir::ArrayRef<sim::SimRefAllocOp> allocations) const;
  /// Copy-only loads whose source still holds the loaded version at every use.
  /// Dense must-dataflow intersects versions at CFG/region joins and kills
  /// them on possible writes. Consumers must not alias the source either:
  /// lowering may use memcpy, whose source must survive the entire operation.
  llvm::DenseSet<mlir::Value>
  getBorrowableLoads(mlir::ArrayRef<sim::SimRefLoadOp> loads) const;
  sim::SimRefAllocOp lookup(mlir::Value reference) const {
    return references.lookup(reference);
  }
  bool isLocalAccess(mlir::Operation *operation) const {
    if (auto allocation = mlir::dyn_cast<sim::SimRefAllocOp>(operation))
      return bool(lookup(allocation.getResult()));
    if (auto load = mlir::dyn_cast<sim::SimRefLoadOp>(operation))
      return bool(lookup(load.getReference()));
    if (auto store = mlir::dyn_cast<sim::SimRefStoreOp>(operation))
      return bool(lookup(store.getReference()));
    return false;
  }
  const llvm::DenseMap<mlir::Value, sim::SimRefAllocOp> &getReferences() const {
    return references;
  }

private:
  sim::SimFuncOp function;
  llvm::DenseMap<mlir::Value, sim::SimRefAllocOp> references;
};

} // namespace obelisk::analysis
#endif
