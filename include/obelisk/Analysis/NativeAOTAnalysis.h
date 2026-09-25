//===- NativeAOTAnalysis.h - Native scheduler eligibility ------*- C++ -*-===//
//
// Read-only whole-module analysis for native AOT scheduler eligibility.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H
#define OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H

#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <string>

namespace obelisk::analysis {

/// Certify the compiler-generated one-shot transport commit used by Clause
/// 31.9 delayed timing terminals. The marker is only a provenance gate; the
/// function's complete CFG and effects are checked structurally.
bool isNegativeTimingDelayCommit(sim::SimFuncOp function);

/// Certify the unique source-monitor activation allowed to spawn a Clause
/// 31.9 delayed commit.  Both the complete monitor CFG and the target commit
/// are checked; arbitrary callers cannot inherit the cold-boundary policy.
bool isNegativeTimingDelayMonitorSpawn(sim::SimSpawnOp spawn,
                                       sim::SimFuncOp target);

/// Certify the complete compiler-generated Clause 31.9 delayed-terminal
/// monitor, including its unique transport-commit activation.
bool isNegativeTimingDelayMonitor(sim::SimFuncOp function);

/// Certify a compiler-generated Clause 16.14/31 clock-set coordinator whose
/// occurrence-cohort wait must remain owned by the generic scheduler.
bool isRuntimeClockCoordinator(sim::SimFuncOp function);

/// Certify the compiler-generated covergroup clock-event registration actor.
/// The actor installs one runtime-owned observer during bootstrap and then
/// parks forever, so it is a cold hybrid boundary rather than a schedulable
/// process body.
bool isCovergroupClockingSamplerActor(sim::SimFuncOp function);

/// Graph cost withheld from native scheduling by one boundary reason.
///
/// A fragment can touch several reasons, so `cost` and `fragments` may overlap
/// between entries and do not sum to the withheld total. The exact partition is
/// `getNativeGraphCost()`, `getExcludedActorCost()` and
/// `getExcludedBlockCost()`.
struct NativeAOTBoundaryCost {
  std::string reason;
  /// Graph cost of fragments this reason contributed to withholding.
  uint64_t cost = 0;
  /// Fragments this reason contributed to withholding.
  uint32_t fragments = 0;
  /// Actors this reason removed from the static inventory outright.
  uint32_t actors = 0;
};

/// Immutable native-scheduler eligibility facts for one module.
///
/// `eligible` means at least one statically bound actor can use native AOT
/// scheduling. `fullyEligible` additionally means the complete design avoids
/// every recorded generic or bytecode boundary.
class NativeAOTAnalysis {
public:
  NativeAOTAnalysis() = default;

  static NativeAOTAnalysis compute(mlir::ModuleOp module);

  bool isEligible() const { return eligible; }
  bool isFullyEligible() const { return fullyEligible; }
  /// True when every non-AOT boundary is compiler-generated cold concurrent
  /// assertion work: detached report callbacks or asynchronous disable
  /// observers. Explicit AOT may use the hybrid coordinator for these actors
  /// while retaining the statically bound monitor actors.
  bool isForcedHybridEligible() const { return forcedHybridEligible; }
  /// Whether Auto has enough native work for a closed schedule, or for a
  /// partial periodic island. Lowering still verifies exact fanout and direct
  /// eval ownership.
  bool isAOTCostEffective() const { return aotCostEffective; }
  bool hasPeriodicClockCandidate() const { return periodicClockCandidate; }
  uint64_t getTotalGraphCost() const { return totalGraphCost; }
  uint64_t getNativeGraphCost() const { return nativeGraphCost; }
  /// Graph cost in actors removed from the static inventory outright.
  uint64_t getExcludedActorCost() const { return excludedActorCost; }
  /// Graph cost in admitted actors' individual bytecode blocks.
  uint64_t getExcludedBlockCost() const { return excludedBlockCost; }
  mlir::ArrayRef<std::string> getReasons() const { return reasons; }
  /// Per-reason attribution of the withheld graph cost, most expensive first.
  mlir::ArrayRef<NativeAOTBoundaryCost> getBoundaryCosts() const {
    return boundaryCosts;
  }

  const llvm::DenseMap<mlir::Operation *, uint32_t> &getActorSlots() const {
    return actorSlots;
  }
  const llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Block *>> &
  getBytecodeFragments() const {
    return bytecodeFragments;
  }
  const llvm::DenseSet<mlir::Operation *> &
  getRuntimeObservedWriterActors() const {
    return runtimeObservedWriterActors;
  }
  const llvm::DenseSet<mlir::Operation *> &getRuntimeOwnedFanoutActors() const {
    return runtimeOwnedFanoutActors;
  }
  const llvm::DenseSet<mlir::Operation *> &
  getNegativeTimingFanoutActors() const {
    return negativeTimingFanoutActors;
  }

private:
  bool eligible = false;
  bool fullyEligible = false;
  bool forcedHybridEligible = false;
  bool aotCostEffective = false;
  bool periodicClockCandidate = false;
  uint64_t totalGraphCost = 0;
  uint64_t nativeGraphCost = 0;
  uint64_t excludedActorCost = 0;
  uint64_t excludedBlockCost = 0;
  mlir::SmallVector<std::string> reasons;
  mlir::SmallVector<NativeAOTBoundaryCost> boundaryCosts;
  llvm::DenseMap<mlir::Operation *, uint32_t> actorSlots;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Block *>>
      bytecodeFragments;
  llvm::DenseSet<mlir::Operation *> runtimeObservedWriterActors;
  llvm::DenseSet<mlir::Operation *> runtimeOwnedFanoutActors;
  llvm::DenseSet<mlir::Operation *> negativeTimingFanoutActors;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H
