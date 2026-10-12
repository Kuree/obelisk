//===- KnownVersioningAnalysis.h - Regions that can run on value planes ---===//
//
// Finds single-entry regions of a native activation body that become entirely
// two-state once one check at their entry finds every four-state input known.
// Such a region keeps its four-state body for X/Z inputs and runs a two-state
// copy otherwise: a natural loop switches copies at every iteration boundary,
// and an acyclic run checks once per execution.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_KNOWNVERSIONINGANALYSIS_H
#define OBELISK_ANALYSIS_KNOWNVERSIONINGANALYSIS_H

#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace obelisk::sim {
class SimFuncOp;
}

namespace obelisk::analysis {

/// A read of possibly four-state memory whose reference the region entry can
/// recompute. `slice` lists, in dominance order, the pure in-region producers
/// of the reference.
struct KnownRegionRead {
  mlir::Operation *read = nullptr;
  llvm::SmallVector<mlir::Operation *> slice;
};

enum class KnownRegionKind { Loop, Run };

/// A region whose execution is two-state once its entry check passes.
///
/// Every operation maps known operands to known results under the shared
/// IEEE 1800-2023 value-domain transfer. The four-state inputs are the logic
/// live-ins (for a loop, also the header's logic arguments) and the reads in
/// `checkedReads`. A later read observes either its checked location or a
/// value the two-state copy stored, which is known. A read in `ownedReads`
/// goes through storage the region allocates from known values, so it only
/// observes known values. A selection in `rangeProved` is in range only by a
/// range proof; the two-state copy keeps that proof by casting its result.
/// The only effects are reads, blocking stores and private allocations, so
/// switching copies at the entry never repeats or skips an effect.
struct KnownRegion {
  KnownRegionKind kind = KnownRegionKind::Run;
  /// A loop's header; null for a run.
  mlir::Block *header = nullptr;
  /// A run spans the operations from `begin` up to, excluding, `end`. Both are
  /// in blocks of one dominator chain, and every block in between belongs to
  /// the run. Null for a loop.
  mlir::Operation *begin = nullptr;
  mlir::Operation *end = nullptr;
  llvm::SmallVector<mlir::Value> liveIns;
  llvm::SmallVector<KnownRegionRead, 0> checkedReads;
  llvm::SmallVector<mlir::Operation *> ownedReads;
  llvm::SmallVector<mlir::Operation *> rangeProved;
  /// Operations copied into the two-state version.
  unsigned operations = 0;
  /// All operations versioning adds: the copy, the entry check (each read's
  /// reference slice and probe, each live-in's test) and the casts.
  unsigned addedOperations = 0;
  /// Estimated unknown-plane instructions removed per execution (per
  /// iteration of a loop), counting only operations the function's own
  /// facts cannot already prove two-state.
  unsigned benefit = 0;
  /// Estimated check instructions added per execution (per iteration).
  unsigned checkCost = 0;
};

/// The first reason a candidate stays four-state.
struct RejectedKnownRegion {
  KnownRegionKind kind = KnownRegionKind::Run;
  mlir::Block *block = nullptr;
  llvm::StringRef reason;
  mlir::Operation *operation = nullptr;
};

/// Read-only analysis of one activation body without suspension.
///
/// Loops are considered outermost first; an admitted loop owns its nested
/// loops, and the children of a rejected loop are considered in turn. Runs
/// partition the acyclic code outside admitted loops along the dominator
/// chains. A region is profitable when its estimated benefit exceeds its check
/// cost. Added code is bounded by half the function's size (or a small fixed
/// allowance), filled with the regions that remove the most estimated work per
/// added operation first. The function must not change while results are used.
class KnownVersioningAnalysis {
public:
  /// `provenKnown` adds facts the function already relies on, such as the
  /// guarded inductive facts of a two-state variant: such a value needs no
  /// check, and work on it is not counted as removable.
  ///
  /// `expectedKnown`, when given, predicts steady-state knownness, for example
  /// two-state once the design's inductively two-state roots are known. A
  /// region whose four-state input is not expected known would fail its check
  /// on most executions and pay for it, so it is not versioned.
  explicit KnownVersioningAnalysis(
      sim::SimFuncOp function,
      llvm::function_ref<bool(mlir::Value)> provenKnown = nullptr,
      llvm::function_ref<bool(mlir::Value)> expectedKnown = nullptr);
  ~KnownVersioningAnalysis();

  llvm::ArrayRef<KnownRegion> getRegions() const { return regions; }
  llvm::ArrayRef<RejectedKnownRegion> getRejected() const { return rejected; }

  /// Whether a value of `type` has a two-state view built by
  /// `logic.to_bits`/`logic.from_bits`: a four-state scalar or a packed
  /// aggregate of four-state members.
  static bool isCastable(mlir::Type type);

private:
  llvm::SmallVector<KnownRegion, 0> regions;
  llvm::SmallVector<RejectedKnownRegion> rejected;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_KNOWNVERSIONINGANALYSIS_H
