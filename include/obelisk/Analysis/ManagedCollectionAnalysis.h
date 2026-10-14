//===- ManagedCollectionAnalysis.h - Managed heap collection points -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_MANAGEDCOLLECTIONANALYSIS_H
#define OBELISK_ANALYSIS_MANAGEDCOLLECTIONANALYSIS_H

#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace obelisk::sim {
class SimFuncOp;
}

namespace obelisk::analysis {

/// An operation that may allocate managed storage itself: one with the
/// `AllocatesManagedStorage` trait, or a DPI call that converts managed values
/// across its boundary. Calls into compiled code are summarized separately by
/// ManagedCollectionAnalysis.
bool mayAllocateManagedStorage(mlir::Operation *operation);

/// A call that runs compiled code and returns to its caller within the same
/// activation: a function, method, virtual method, or DPI import call. Only
/// these can be marked poll-free.
bool isCompiledCall(mlir::Operation *operation);

/// Where the managed heap may reclaim storage under polled collection.
///
/// Compiled code exposes its managed SSA values to the collector only at
/// `simulation.gc.safepoint` polls and at calls that may reach one; allocation
/// records pressure but never collects. Between activations nothing holds an
/// unregistered value, so the scheduler reclaims there. Garbage allocated
/// between two such opportunities stays bounded when every way to allocate
/// repeatedly without returning to the scheduler passes a poll:
///   - an allocation in a control-flow cycle that avoids every suspension
///     returning to the scheduler,
///   - an allocating call that may re-enter its caller (recursion, or a
///     callee this analysis cannot resolve),
///   - the entry of an allocating function foreign code may call repeatedly
///     (a DPI export).
/// The call graph resolves virtual dispatch over the closed class hierarchy
/// and connects context DPI imports to every export (IEEE 1800-2023 35.5.3:
/// only context imports may call exported subroutines).
class ManagedCollectionAnalysis {
public:
  enum Summary : unsigned {
    /// Executing the function may allocate managed storage.
    Allocates = 1,
    /// Executing the function may reach a GC poll.
    Polls = 2
  };

  /// Summarize every function under `root`, counting the polls
  /// `getPollSites` places as already present.
  explicit ManagedCollectionAnalysis(mlir::Operation *root);

  /// What executing `function` to completion may do, including its callees.
  unsigned getSummary(sim::SimFuncOp function) const;
  /// What a call into compiled code may do before it returns: the union of
  /// its callee summaries, or every effect when a callee is unresolved. Zero
  /// for operations that do not call compiled code.
  unsigned getCallSummary(mlir::Operation *call) const;
  /// Operations before which a poll is required, in program order. A
  /// function's entry poll is reported as its first entry-block operation.
  llvm::ArrayRef<mlir::Operation *> getPollSites(sim::SimFuncOp function) const;

private:
  llvm::DenseMap<mlir::Operation *, unsigned> summaries;
  llvm::DenseMap<mlir::Operation *, unsigned> callSummaries;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>>
      pollSites;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_MANAGEDCOLLECTIONANALYSIS_H
