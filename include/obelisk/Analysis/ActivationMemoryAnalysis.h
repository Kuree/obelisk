//===- ActivationMemoryAnalysis.h - Activation memory facts ----*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONMEMORYANALYSIS_H
#define OBELISK_ANALYSIS_ACTIVATIONMEMORYANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

namespace obelisk::analysis {

/// Dense, source-ordered must-definition analysis of one activation. Bounded
/// induction values partition the lattice; unknown input branches join by
/// intersection. Partitioning is analysis-only: loops are never replicated in
/// executable IR. Unknown loop-carried values join to conservative may facts;
/// exhaustion of the partition budget invalidates the result.
///
/// This proves memory coverage, not permission to coalesce publications or to
/// execute an inactive process. In particular, repeated writes require a
/// separate whole-program observation/activation proof.
class ActivationMemoryAnalysis {
public:
  enum class DependencyMode { Accesses, ReachingValues };
  struct Range {
    uint64_t root, low, width;
    bool operator==(const Range &other) const {
      return root == other.root && low == other.low && width == other.width;
    }
  };
  using Ranges = llvm::SmallVector<Range>;
  struct Publication {
    Range output;
    Ranges inputs;
  };
  ActivationMemoryAnalysis() = default;
  ActivationMemoryAnalysis(sim::SimFuncOp function,
                           const HandleDataflowAnalysis &handles,
                           unsigned maximumPartitions = 65536,
                           DependencyMode mode = DependencyMode::Accesses);
  bool isValid() const { return valid; }
  /// Reads whose values may come from before this activation.
  llvm::ArrayRef<Range> getLiveIns() const { return liveIns; }
  /// Incoming bits influencing publications, control decisions or returns.
  /// Available only in ReachingValues mode, on a closed activation CFG.
  /// Unlike physical live-ins, these follow reaching stores and SSA lanes.
  llvm::ArrayRef<Range> getValueInputs() const { return valueInputs; }
  bool hasValueDependencies() const { return valid && valueDependencies; }
  /// Per-access, per-output-lane dependence. These facts do not authorize
  /// reordering effects, removing sensitivity, or hiding intermediate writes.
  const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<Publication>> &
  getPublications() const {
    return publications;
  }
  llvm::ArrayRef<Range> getMayWrites() const { return mayWrites; }
  /// Ranges definitely assigned on every terminating activation path.
  llvm::ArrayRef<Range> getMustWrites() const { return mustWrites; }
  /// Ranges which may receive more than one write during this activation.
  llvm::ArrayRef<Range> getRepeatedWrites() const { return repeatedWrites; }
  const llvm::DenseMap<mlir::Operation *, Ranges> &getReads() const {
    return reads;
  }
  const llvm::DenseMap<mlir::Operation *, Ranges> &getWrites() const {
    return writes;
  }
  unsigned getPartitionCount() const { return partitions; }
  bool hasCompleteWrites() const;

private:
  bool valid = false;
  bool valueDependencies = false;
  unsigned partitions = 0;
  Ranges liveIns, valueInputs, mayWrites, mustWrites, repeatedWrites;
  llvm::DenseMap<mlir::Operation *, Ranges> reads, writes;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<Publication>>
      publications;
};
} // namespace obelisk::analysis
#endif
