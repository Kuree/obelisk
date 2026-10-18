//===- ActivationMemoryAnalysis.h - Activation memory facts ----*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONMEMORYANALYSIS_H
#define OBELISK_ANALYSIS_ACTIVATIONMEMORYANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/RangeSet.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"

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
  enum class DependencyMode { Accesses, Inputs, ReachingValues };
  /// Descriptor namespaces are disjoint in the activation graph. Bit 63 is
  /// reserved for private slots; bit 62 distinguishes nets from storage.
  static constexpr uint64_t netRootTag = uint64_t{1} << 62;
  static std::optional<uint64_t> getRoot(schedule::ComputeResourceKind resource,
                                         uint64_t descriptor) {
    if (descriptor >= netRootTag)
      return std::nullopt;
    if (resource == schedule::ComputeResourceKind::Storage)
      return descriptor;
    if (resource == schedule::ComputeResourceKind::Net)
      return descriptor | netRootTag;
    return std::nullopt;
  }
  using Range = analysis::Range;
  using Ranges = analysis::Ranges;
  /// Values and handles of admitted immediate publications.
  static mlir::Value getStoredValue(mlir::Operation *operation) {
    if (auto store = mlir::dyn_cast<sim::SimRefStoreOp>(operation))
      return store.getValue();
    return mlir::cast<sim::SimDriverDriveOp>(operation).getValue();
  }
  static mlir::Value getWriteHandle(mlir::Operation *operation) {
    if (auto store = mlir::dyn_cast<sim::SimRefStoreOp>(operation))
      return store.getReference();
    return mlir::cast<sim::SimDriverDriveOp>(operation).getDriver();
  }
  struct Publication {
    Range output;
    Ranges inputs;
  };
  ActivationMemoryAnalysis() = default;
  /// `symbols` resolves callees when `function` is a detached activation
  /// view; it defaults to the function's own parent. Calls of side-effect-free
  /// functions are computations. Activation-local `ref.alloc` slots are private
  /// state: their accesses are neither reads nor writes of the design.
  ActivationMemoryAnalysis(sim::SimFuncOp function,
                           const HandleDataflowAnalysis &handles,
                           unsigned maximumPartitions = 65536,
                           DependencyMode mode = DependencyMode::Accesses,
                           mlir::Operation *symbols = nullptr);
  bool isValid() const { return valid; }
  /// The first operation without a memory summary, when invalid.
  mlir::Operation *getUnsupportedOperation() const { return unsupported; }
  /// Reads whose values may come from before this activation.
  llvm::ArrayRef<Range> getLiveIns() const { return liveIns; }
  /// Incoming bits influencing publications, control decisions or returns.
  /// Available in Inputs and ReachingValues modes, on a closed activation CFG.
  /// Unlike physical live-ins, these follow reaching stores and SSA lanes.
  llvm::ArrayRef<Range> getValueInputs() const { return valueInputs; }
  bool hasInputDependencies() const { return valid && inputDependencies; }
  /// Why precise dependencies were unavailable; empty when the proof succeeds.
  llvm::StringRef getDependencyFailure() const { return dependencyFailure; }
  /// Sparse value nodes, including context proxies and publication sinks.
  unsigned getValueNodeCount() const { return valueNodes; }
  /// Incoming bits influencing final written lanes and returns. Intermediate
  /// publications are still described by getValueInputs()/getPublications().
  /// Using only these final dependencies requires a separate proof that no
  /// observer can distinguish the intermediate transitions.
  llvm::ArrayRef<Range> getFinalInputs() const { return finalInputs; }
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
  /// Loads that may also read a private slot, including a selection between
  /// private and canonical references. Slicing must retain their local stores.
  const llvm::DenseSet<mlir::Operation *> &getPrivateReads() const {
    return privateReads;
  }
  /// A store may update private scratch or publish canonical storage. The
  /// input summary accounts for both, but publication slicing cannot erase or
  /// duplicate that store independently of its private consumers.
  bool hasMixedPrivateWrites() const { return mixedPrivateWrites; }
  const llvm::DenseMap<mlir::Operation *, Ranges> &getWrites() const {
    return writes;
  }
  unsigned getPartitionCount() const { return partitions; }
  bool hasCompleteWrites() const;

private:
  bool valid = false;
  mlir::Operation *unsupported = nullptr;
  bool valueDependencies = false;
  bool inputDependencies = false;
  bool mixedPrivateWrites = false;
  unsigned partitions = 0;
  unsigned valueNodes = 0;
  llvm::StringRef dependencyFailure = "not requested";
  Ranges liveIns, valueInputs, finalInputs, mayWrites, mustWrites,
      repeatedWrites;
  llvm::DenseMap<mlir::Operation *, Ranges> reads, writes;
  llvm::DenseSet<mlir::Operation *> privateReads;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<Publication>>
      publications;
};
} // namespace obelisk::analysis
#endif
