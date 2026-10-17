//===- LocalUnknownAnalysis.h - X/Z inside one gated activation -*- C++ -*-===//
//
// Classifies the canonical state reads of one activation body for a copy that
// runs only after a check at the activation's entry finds selected canonical
// bits known. In that copy a *gated* read loads only its value plane, because
// its bits are known whenever it executes; every other read is an *island*
// that keeps both planes. Stores still write both planes, so values computed
// from islands remain exact four-state values and the copy performs the same
// effects with the same values as the original body.
//
// A gated read observes known bits when (1) the entry check covered them and
// (2) no write of the activation that can precede it may have stored X/Z into
// them. (2) is a dense forward fact over the activation's CFG; the stored
// values come from a sparse forward state-domain fact in which gated reads
// are known. Both are solved together, and their least fixed point is sound
// by induction over the activation's execution order: before the first X/Z
// write every gated read sees entry state, and a value the domain fact proves
// known given known gated reads is known (IEEE 1800-2023 11.4 for each
// operator, with the X/Z generators of 7.4.6, 11.4.2, 11.5.1 and 6.8 kept
// four-state by the shared transfer).
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
// Reads become islands when gating them would make the entry check fail in
// steady state or cost more than it saves:
//  - a dynamically indexed unpacked array element: a RAM-like memory whose
//    entries need not all have been written (6.8, 7.4.6);
//  - a dynamic packed selection that may be invalid, which reads X/Z
//    whatever the storage holds (7.4.6, 11.5.1), or whose hull is too wide;
//  - a read whose value only moves into stores, NBAs and displays: keeping
//    its unknown plane costs only the copy, while gating it would make the
//    check fail whenever the moved data holds X/Z;
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
    /// A dynamically indexed unpacked array element, or a dynamic selection
    /// that may be invalid in the copy or whose hull is too wide.
    Dynamic,
    /// Only moved into stores, NBAs and displays.
    Moved,
    /// An X/Z write of the activation may reach it.
    Written,
    /// Tested where it loads; the copy continues in the original body at
    /// this read when it holds X/Z, and treats it as known after.
    Checked,
  };

  /// Bits [low, low + width) of one storage root, relative to the root.
  struct Range {
    uint64_t low = 0, width = 0;
  };
  /// The bits the entry check covers in one root: sorted, disjoint, and not
  /// adjacent.
  struct GateRoot {
    uint64_t descriptor = 0;
    llvm::SmallVector<Range, 2> ranges;
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
    llvm::SmallVector<GateRoot, 0> gate;
    uint64_t gateWords = 0;
    uint64_t benefit = 0;
    /// Reads the copy tests where they load.
    unsigned checks = 0;
    /// Private allocations whose contents the entry check tests: known reads
    /// of the copy find them as the previous segment left them.
    llvm::SetVector<sim::SimRefAllocOp> slotChecks;
    /// The part of `gate` the previous segment's copy leaves known: its
    /// checked and overwritten bits that no X/Z write may reach. The check
    /// skips them when that copy ran.
    llvm::SmallVector<GateRoot, 0> inherited;
    /// The rest of `gate`, which the check always tests, and its words.
    llvm::SmallVector<GateRoot, 0> fresh;
    uint64_t freshWords = 0;
    /// Four-state values from other segments that only the copy proves
    /// known: the entry check tests them.
    llvm::SetVector<mlir::Value> liveIns;
    /// Some value from another segment that only the copy proves known has
    /// no fixed bit image to test, so the segment cannot be versioned.
    bool untestable = false;
  };

  /// What a caller may assume about one callee.
  struct CalleeSummary {
    /// The callee may write design storage or a net, or has effects the
    /// analysis does not model.
    bool writes = true;
    /// Its results are known whenever every four-state argument is.
    bool knownResults = false;
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
  /// Canonical reads in walk order.
  llvm::ArrayRef<mlir::Operation *> getReads() const { return reads; }
  llvm::ArrayRef<Segment> getSegments() const { return segments; }
  /// The first segment's check and savings: the whole body's without a
  /// segmentation.
  llvm::ArrayRef<GateRoot> getGate() const {
    return segments.empty() ? llvm::ArrayRef<GateRoot>() : segments[0].gate;
  }
  /// The 64-bit words the entry check tests, counting each range from its
  /// first word.
  uint64_t getGateWords() const {
    return segments.empty() ? 0 : segments[0].gateWords;
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
  /// Why no read can be gated, or empty.
  llvm::StringRef getFailure() const { return failure; }

  static llvm::StringRef stringify(ReadKind kind);

private:
  llvm::SmallVector<mlir::Operation *, 0> reads;
  llvm::DenseMap<mlir::Operation *, ReadKind> kinds;
  llvm::SmallVector<Segment, 0> segments;
  llvm::DenseSet<mlir::Value> knownInCopy, knownInOriginal;
  llvm::StringRef failure;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_LOCALUNKNOWNANALYSIS_H
