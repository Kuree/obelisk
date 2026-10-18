//===- LocalUnknownAnalysis.h - X/Z inside one gated activation -*- C++ -*-===//
//
// Classifies the canonical state accesses of one activation body for a copy
// that runs only after a check at the activation's entry finds whole storage
// roots known (schedule.known.roots): a tested root holds X/Z exactly in the
// bits of its steady pattern (`simulation.steady_unknown`), none elsewhere.
// In that copy a *gated* read loads only its value plane, and a *known* store
// writes only its value plane: the root's unknown plane is zero at the stored
// bits, and a known value keeps it zero. Every other read and store keeps both
// planes, so the copy performs the same effects with the same values as the
// original body.
//
// A gated read observes known bits when (1) the entry check covered its root
// and (2) no write of the activation that can precede it may have stored X/Z
// into its bits. A known store needs the same of the bits it writes, and a
// value known whenever the copy computes it. (2) is a dense forward fact over
// the activation's CFG, kept per bit in persistent range sets; the stored
// values come from a sparse forward fact giving each value the bits that may
// hold X/Z, in which gated reads are known. Both are solved together, and
// their least fixed point is sound by induction over the activation's
// execution order: before the first X/Z write every gated read sees entry
// state, and a value the bit fact proves known given known gated reads is
// known: moves carry their bits, bitwise operators work per bit, and an X/Z
// operand bit makes arithmetic, relations, reductions and logical operators
// X (IEEE 1800-2023 11.4), with the X/Z generators of 6.8, 7.4.5, 11.4.3 and
// 11.5.1 kept four-state. A field of a partially assigned aggregate, or a
// private slot's bits, are therefore known on their own. The lowering's own
// tests for X/Z in a value (`x === view(x)`, 11.4.6) fold to true in the
// copy for a known `x`, so the paths that merge X/Z are dead there.
//
// A call to a function whose summary records the formals it stores through
// (`CalleeSummary::storedFormals`) adds X/Z to what those formals reference
// only when the stored values may hold X/Z; any other write of a callee makes
// every storage root and net unknown. Private allocations passed to such
// calls stay private: a sibling function cannot retain a reference (13.5).
//
// The body may be cut into segments, each checked at its own entry: a run of
// the activation's operations that control enters only at its first
// operation. A check at a segment entry observes the canonical planes after
// every earlier write, so only writes inside the segment can precede a gated
// read unchecked. A value computed in another segment has the domain its own
// copy gives it, but comes from whichever copy ran there: the entry check
// tests each one that only the copy proves known (`Segment::liveIns`). Private
// slots likewise hold what either copy stored: a known read of a slot whose
// contents may predate the segment needs the check to test the slot
// (`Segment::slotChecks`). Storage bits that every path of the segment stored
// before a read need no test of their own.
//
// A read can instead be tested where it loads: when it holds X/Z the copy
// continues in the original body at that read, carrying its live values, so
// nothing runs twice and only the bits the read observes are tested.
//
// A gated read of pattern bits loads them as the pattern's constant X, so
// its value stays four-state. Any write of the segment that may reach
// pattern bits before such a read keeps the read's planes: it may have made
// them known. A store reaching pattern bits is never a known store.
//
// Reads keep both planes when gating them would be unsound:
//  - a dynamic selection that may be invalid even with known selectors, or
//    whose selectors may hold X/Z in the copy: it reads the type's default,
//    X/Z for four-state types (7.4.5, 11.5.1);
//  - a read an X/Z write of the activation may reach;
//  - a read whose root is not a fixed design storage root.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_LOCALUNKNOWNANALYSIS_H
#define OBELISK_ANALYSIS_LOCALUNKNOWNANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <utility>

namespace obelisk::sim {
class SimFuncOp;
}

namespace obelisk::analysis {

class LocalUnknownAnalysis {
public:
  enum class ReadKind : uint8_t {
    /// Loads only its value plane in the gated copy.
    Gated,
    /// Carries no unknown plane.
    TwoState,
    /// Never executes.
    Dead,
    /// Function-private storage.
    Local,
    /// Not a fixed design storage root.
    Unresolved,
    /// A dynamic selection that may be invalid in the copy (7.4.5, 11.5.1).
    Dynamic,
    /// An X/Z write of the activation may reach it.
    Written,
    /// Tested where it loads; the copy continues in the original body at
    /// this read when it holds X/Z, and treats it as known after.
    Checked,
  };

  /// Segments of a body: `segments` maps every operation, nested ones
  /// included, to its segment, and `begins` holds the first operation of
  /// every segment after the first. Without one the body is one segment.
  struct Segmentation {
    llvm::DenseMap<mlir::Operation *, unsigned> segments;
    llvm::DenseSet<mlir::Operation *> begins;
    unsigned count = 1;
  };
  /// What the entry check of one segment tests, and what the copy saves.
  struct Segment {
    /// The storage roots the check tests, by descriptor, sorted: those of
    /// the segment's gated reads and known stores.
    llvm::SmallVector<uint64_t, 0> gate;
    uint64_t benefit = 0;
    /// Reads the copy tests where they load.
    unsigned checks = 0;
    /// Private allocations whose contents the entry check tests: known reads
    /// of the copy find them as the previous segment left them.
    llvm::SetVector<sim::SimRefAllocOp> slotChecks;
    /// Four-state values from other segments that only the copy proves
    /// known: the entry check tests them.
    llvm::SetVector<mlir::Value> liveIns;
    /// Some value from another segment that only the copy proves known has
    /// no fixed bit image to test, so the segment cannot be versioned.
    bool untestable = false;
  };

  /// What a caller may assume about one callee.
  struct CalleeSummary {
    /// The callee may write design storage or a net other than through the
    /// references in `storedFormals`, or has effects the analysis does not
    /// model.
    bool writes = true;
    /// Its results are known whenever every four-state argument is.
    bool knownResults = false;
    /// The formals whose referenced storage the callee may store to, sorted.
    llvm::SmallVector<unsigned, 2> storedFormals;
    /// Every such store writes a value known whenever every four-state
    /// argument is.
    bool knownFormalStores = false;
  };
  /// Summaries by function name.
  using CalleeSummaries = llvm::StringMap<CalleeSummary>;
  /// Summarize every called function of `design`, callees before callers.
  /// The functions of a recursive cycle keep the conservative default.
  static CalleeSummaries summarize(sim::SimDesignOp design,
                                   const HandleDataflowAnalysis &handles);

  struct Options {
    const Segmentation *segmentation = nullptr;
    /// Calls to other functions use these summaries; without one a call may
    /// write anything and returns four-state results.
    const CalleeSummaries *callees = nullptr;
    /// Summarize a callee: its four-state arguments are known and no read is
    /// gated.
    bool summary = false;
    /// Reads the function already proves known, such as the inductively
    /// two-state reads of a guarded two-state variant: known in both copies
    /// and never gated.
    llvm::function_ref<bool(mlir::Operation *)> provenKnownRead = nullptr;
    /// Whether reads may be tested where they load.
    bool checks = false;
    /// Whether bits [low, high) of root `descriptor` include bits of its
    /// steady pattern (`simulation.steady_unknown`). A gated read of such
    /// bits loads them as a constant X, so its value stays four-state; a
    /// dynamic selection over them is not gated.
    llvm::function_ref<bool(uint64_t, uint64_t, uint64_t)> staysUnknown =
        nullptr;
  };

  /// `handles` must describe the references of `function`.
  LocalUnknownAnalysis(sim::SimFuncOp function,
                       const HandleDataflowResult &handles,
                       const Options &options);
  LocalUnknownAnalysis(sim::SimFuncOp function,
                       const HandleDataflowResult &handles)
      : LocalUnknownAnalysis(function, handles, Options()) {}

  ReadKind getKind(mlir::Operation *read) const;
  bool isGated(mlir::Operation *read) const {
    return getKind(read) == ReadKind::Gated;
  }
  /// Whether `store` writes only its value plane in the gated copy.
  bool isKnownStore(mlir::Operation *store) const {
    return knownStores.contains(store);
  }
  /// Canonical reads in walk order.
  llvm::ArrayRef<mlir::Operation *> getReads() const { return reads; }
  llvm::ArrayRef<Segment> getSegments() const { return segments; }
  /// The first segment's check and savings: the whole body's without a
  /// segmentation.
  llvm::ArrayRef<uint64_t> getGate() const {
    return segments.empty() ? llvm::ArrayRef<uint64_t>() : segments[0].gate;
  }
  /// Estimated unknown-plane instructions the gated copy removes, counted
  /// once per operation.
  uint64_t getBenefit() const {
    return segments.empty() ? 0 : segments[0].benefit;
  }
  /// Whether `value` is known whenever the gated copy computes it.
  bool isKnownInCopy(mlir::Value value) const {
    return knownInCopy.contains(value);
  }
  /// Whether `value` is known in the original body as well.
  bool isKnownInOriginal(mlir::Value value) const {
    return knownInOriginal.contains(value);
  }
  /// Whether `block` never executes in the gated copy.
  bool isDeadBlock(mlir::Block *block) const {
    return deadBlocks.contains(block);
  }
  /// Why no read can be gated, or empty.
  llvm::StringRef getFailure() const { return failure; }

  static llvm::StringRef stringify(ReadKind kind);

private:
  llvm::SmallVector<mlir::Operation *, 0> reads;
  llvm::DenseMap<mlir::Operation *, ReadKind> kinds;
  llvm::DenseSet<mlir::Operation *> knownStores;
  llvm::SmallVector<Segment, 0> segments;
  llvm::DenseSet<mlir::Value> knownInCopy, knownInOriginal;
  llvm::DenseSet<mlir::Block *> deadBlocks;
  llvm::StringRef failure;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_LOCALUNKNOWNANALYSIS_H
