//===- ComputeFusion.h - Static process-body fusion helpers -----*- C++ -*-===//

#ifndef OBELISK_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H
#define OBELISK_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H

#include "obelisk/Analysis/ActivationRegionAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace obelisk {

/// A finite combinational activation with complete, exclusive outputs. Live-in
/// reads are watched; structured activations may also read their own definite
/// assignments. Repeated publications need the separate observer certificate.
/// This is deliberately not an always_latch or arbitrary always certificate.
struct CombinationalFusionBody {
  mlir::Block *activation;
  mlir::Operation *suspend;
  mlir::SmallVector<mlir::Block *> blocks;
  mlir::SmallVector<mlir::Value> outputs;
  const analysis::ActivationRegionAnalysis *structured = nullptr;
};

struct CombinationalFusionFailure {
  enum class Reason {
    None,
    UnsupportedOwner,
    Activation,
    IncompleteWrites,
    SensitivityRegion,
    ImpreciseSensitivity,
    UnwatchedLiveIn,
    IncomingOutputState,
    MissingWriterInventory,
    NonExclusiveWriter,
    ActivationEntry,
    UnsupportedObservations,
    MissingActivationProof,
    ToggleCoverage,
    UnknownWatchedResource,
    UnresolvedWatchedStorage,
    OutsideWatch,
  };
  Reason reason = Reason::None;
  analysis::ActivationRegionAnalysis::FailureReason activation =
      analysis::ActivationRegionAnalysis::FailureReason::None;
  uint64_t storage = 0;
  mlir::StringAttr owner;
  /// Format only when emitting a diagnostic; decisions use the enum above.
  std::string getMessage() const;
};

/// Immutable, revision-local writer inventory for local ranked kernels.
class CombinationalFusionAnalysis {
public:
  CombinationalFusionAnalysis(
      sim::SimDesignOp design,
      const analysis::HandleDataflowAnalysis &provenance,
      analysis::ActivationMemoryAnalysis::DependencyMode mode =
          analysis::ActivationMemoryAnalysis::DependencyMode::Accesses);
  std::optional<CombinationalFusionBody>
  analyze(sim::SimFuncOp function,
          const analysis::HandleDataflowAnalysis &provenance,
          bool interleaved = false,
          CombinationalFusionFailure *failureReason = nullptr) const;
  /// Internal transitions can be hidden from the union wait only after all
  /// external watches and toggle observers of repeated writes are excluded.
  bool canInterleave(llvm::ArrayRef<sim::SimFuncOp> functions,
                     CombinationalFusionFailure *failureReason = nullptr) const;

private:
  bool unsupported = false;
  sim::SimDesignOp design;
  analysis::ActivationMemoryAnalysis::DependencyMode dependencyMode;
  mutable llvm::DenseMap<mlir::Operation *,
                         std::unique_ptr<analysis::ActivationRegionAnalysis>>
      activations;
  using Writer = std::pair<mlir::StringAttr, schedule::ComputeEffectAttr>;
  llvm::DenseMap<uint64_t, mlir::SmallVector<Writer>> storageWriters;
  llvm::DenseSet<mlir::StringAttr> nonNativeOwners;
};

/// Return true when a process body can be merged without combining
/// actor-local state or admitting behavior outside the static digital subset.
bool isComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::HandleDataflowAnalysis &provenance);

/// Primitive-only union kernels additionally admit the UDP driver-state read
/// and inertial publication operations that their materializer preserves.
/// General and eval body fusion deliberately retain the narrower contract.
bool isPrimitiveComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::HandleDataflowAnalysis &provenance);

/// Storage descriptors classified by who can observe their intermediate
/// nonblocking updates in one NBA region. IEEE 1800-2017 4.6(b) requires NBAs
/// to be performed in execution order, and 9.4.2 detects an event on each
/// update. Obelisk performs every scheduled NBA of a region before the
/// evaluations they trigger, an order 4.5 permits (the Active region is
/// processed in any order) and 10.4.2 requires for the blocking assignments
/// they create. Under that order a root nothing watches (except a process
/// whose extra activation provably rewrites what it already holds) may merge
/// freely.
struct NBATransientObservers {
  /// Seen by an edge, level or expression wait, VPI or toggle coverage.
  llvm::DenseSet<uint64_t> observable;
  /// Seen only by waits for any change of whole references. A merged commit
  /// is exact for these when it also reports rewritten bits.
  llvm::DenseSet<uint64_t> changeWatched;
  /// Net value-sequence observers seed the influence cone through drives and
  /// aliases. Plain change consumers are covered by exact pending activations.
  llvm::DenseSet<uint64_t> netObservable;
};

/// Classify the design's storage, or std::nullopt when its observers cannot
/// be inventoried.
std::optional<NBATransientObservers>
computeNBATransientObservers(sim::SimDesignOp design);

/// Return continuation targets that can coexist in the Active ready set when
/// the given sensitivity awakens. A constant-delay continuation is excluded
/// only when graph activation edges prove it is the unique producer currently
/// publishing the sensitivity; independent deadlines remain barriers.
mlir::SmallVector<uint32_t>
getComputeFusionReadyTargets(schedule::ComputeGraphAttr graph,
                             schedule::ComputeEffectAttr sensitivity);

} // namespace obelisk

#endif // OBELISK_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H
