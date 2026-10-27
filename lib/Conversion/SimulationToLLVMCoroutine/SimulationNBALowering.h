//===- SimulationNBALowering.h - Native NBA lowering support ----*- C++ -*-===//

#ifndef TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
#define TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H

#include "talos/Dialect/Schedule/ScheduleAttrs.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace mlir {
class TypeConverter;
}

namespace talos::detail {

struct NativeStateLayout;

struct NativeStaticNBAPlan {
  llvm::SmallVector<talos_rt_static_nba_root> roots;
  llvm::SmallVector<uint64_t> rootDescriptors;
  bool completeSourceWriters = false;
  llvm::SmallVector<talos_rt_static_nba_site> sites;
  llvm::DenseMap<uint64_t, uint32_t> siteRoots;
  // Graph rebuilding gives generated eval clones fresh site IDs. Preserve the
  // source semantic site so mutually exclusive compiler clones do not look
  // like independently ordered NBA statements.
  llvm::DenseMap<uint64_t, uint64_t> siteSemanticOrigins;
  llvm::DenseMap<uint64_t, uint64_t> siteWidths;
  // Constant in-range targets certified by handle dataflow at every copy of
  // the site. A missing entry retains the captured dynamic destination.
  llvm::DenseMap<uint64_t, uint64_t> siteFixedOffsets;
  llvm::SmallVector<uint8_t> runtimeQueueRoots;
  // Distinct semantic sites select disjoint fixed root slices / array lanes,
  // or lie on mutually exclusive paths of the same outlined activation.
  // Independent indices cannot alias disjoint lanes. Each site still needs its
  // own once-per-periodic-activation proof and its own generated latch.
  llvm::SmallVector<bool> independentSiteWrites;
  // A root accumulator publishes a single old-to-final transition at the
  // barrier. A second NBA update to the same root can create an intermediate
  // event, even if its bit mask is disjoint from the first update's mask:
  // an event expression spanning both slices can see that value (9.4.2).
  // Event and #0 suspensions can also reenter a single source site before
  // the NBA barrier. Merge only when the plan proves one update per root.
  // IEEE 1800-2017 4.6 requires every NBA to be performed in execution order
  // and 10.4.2 makes each one its own update event, so a root that fails this
  // proof must keep the ordered runtime path.
  llvm::SmallVector<bool> mergeSafeRoots;
  // Source storage lies outside the pre-fusion sparse observer influence cone.
  // Missing proof means false, independently of the once-per-window proof.
  llvm::SmallVector<bool> privateInfluenceRoots;
  // Conditional source-only proof. Full VPI checks visibility independently
  // of value-domain specialization; fine/cold staging keeps its guarded ABI.
  llvm::SmallVector<bool> guardedMergeRoots;
  // No read/watch anywhere in the design, including future activations, and
  // no external observation. Immediate plain NBA enqueues can be erased;
  // evaluation of their SSA operands and delayed events remains separate.
  llvm::SmallVector<bool> unreadRoots;
  // Closed private roots written only by immediate NBAs in one event region
  // after declaration initialization. With no pending NBA, assigning their
  // current four-state value cannot change state before or at the barrier.
  llvm::SmallVector<bool> unchangedSafeRoots;
  // Merge-safe roots whose watchers wait for any change. Each merge into
  // their accumulator ORs bits rewritten with a different value into its
  // transient mask; each commit reports those bits as changed.
  llvm::SmallVector<bool> trackTransients;
  // Roots whose watchers all wait for any change, at any width. A latch that
  // provably stages one update per barrier per bit is exact for them: with no
  // bit written twice there is no intermediate value to lose.
  llvm::SmallVector<bool> changeWatchedRoots;
  llvm::SmallVector<std::string> generatedAccumulators;
  // Canonical state-plane bit offset for each root. This is revision-coupled
  // lowering metadata, not a second state allocation.
  llvm::SmallVector<uint64_t> generatedOffsets;
  // Fixed NBA event region for roots whose every reachable site is a direct
  // scalar stage in the same region. UINT32_MAX denotes a mixed or unsupported
  // root. Region and full-root coverage are separate proofs: fixed partial
  // writes still require a write mask but not valid/region bookkeeping.
  llvm::SmallVector<uint32_t> generatedCommitRegions;
  llvm::SmallVector<bool> generatedFullRootStages;
  // Nonzero when every reachable direct scalar enqueue writes the same fixed
  // root mask. The accumulator value can then be overwritten instead of
  // read-modify-written; repeated activations retain normal last-write wins.
  llvm::SmallVector<uint64_t> generatedFixedWriteMasks;
};

/// Compare a captured chunk with both canonical planes, excluding adjacent
/// packed bits. The caller must prove the captured target is fixed/in-range.
mlir::Value buildNativeNBAChange(mlir::OpBuilder &builder,
                                 mlir::Location location, uint64_t bitOffset,
                                 unsigned width, mlir::Value value,
                                 mlir::Value unknown);

/// Before an NBA write merges into a generated accumulator word, record the
/// staged bits it overwrites with a different value: OR
/// `write_mask & mask & ((value ^ newValue) | (unknown ^ newUnknown))` into
/// `transient`. `newValue` and `newUnknown` are already positioned and masked.
/// Two consecutive writes to a bit differ exactly when some update changed it
/// even though the barrier's final value may equal the old one.
/// `staged`, when given, replaces `write_mask & mask` as the set of bits this
/// barrier already staged; compact stages derive it from the root's dirty bit.
void emitGeneratedNBATransient(mlir::OpBuilder &builder,
                               mlir::Location location, mlir::Value accumulator,
                               uint64_t word, mlir::Value mask,
                               mlir::Value newValue, mlir::Value newUnknown,
                               mlir::Value staged = {});

void populateNBAToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                         mlir::TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         const NativeStateLayout *stateLayout,
                                         bool staticSitesEnabled,
                                         bool guardedClaims, bool evalCeiling,
                                         const llvm::DenseSet<mlir::Operation *>
                                             &completePackedSliceNBAs);
mlir::FailureOr<NativeStaticNBAPlan> buildNativeStaticNBAPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    mlir::ArrayRef<schedule::ComputeNBACommitAttr> orderedCommits,
    bool enabled);
mlir::LogicalResult
materializeGeneratedNBAAccumulators(mlir::ModuleOp module,
                                    const NativeStaticNBAPlan &plan);
mlir::LogicalResult markCleanStaticNBAsInGuardedBodies(
    mlir::ModuleOp module, bool enabled,
    const llvm::DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    mlir::ArrayRef<talos_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout);

} // namespace talos::detail

#endif // TALOS_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
