//===- ComputeFusion.h - Static process-body fusion helpers -----*- C++ -*-===//

#ifndef TALOS_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H
#define TALOS_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H

#include "talos/Analysis/ActivationRegionAnalysis.h"
#include "talos/Analysis/SimulationAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace talos {

/// A finite combinational activation with complete, exclusive outputs. Live-in
/// reads are watched; structured activations may also read their own definite
/// assignments. The explicit reordering/observer contracts additionally permit
/// path-proven idempotent state. Repeated publications need the separate
/// observer certificate.
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
    ObservableState,
    OutsideObserver,
  };
  Reason reason = Reason::None;
  analysis::ActivationRegionAnalysis::FailureReason activation =
      analysis::ActivationRegionAnalysis::FailureReason::None;
  uint64_t storage = 0;
  mlir::StringAttr owner{};
  /// The operation an activation proof could not summarize, if known.
  std::optional<mlir::OperationName> unsupported{};
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
          CombinationalFusionFailure *failureReason = nullptr,
          bool finalValuesOnly = false) const;
  /// Admit overwritten incoming state or a path-proven idempotent transition
  /// only with the full 4.3 observer proof. Such a member must use publication
  /// slicing, which keeps source-local snapshots and their writers together.
  std::optional<CombinationalFusionBody> analyzeForReordering(
      sim::SimFuncOp function,
      const analysis::HandleDataflowAnalysis &provenance,
      CombinationalFusionFailure *failureReason = nullptr) const;
  /// Internal transitions can be hidden from the union wait only after all
  /// external watches and toggle observers of repeated writes are excluded.
  bool canInterleave(llvm::ArrayRef<sim::SimFuncOp> functions,
                     CombinationalFusionFailure *failureReason = nullptr) const;
  /// IEEE 1800-2023 4.3: executing the activations once in data-dependence
  /// order, without each process's statement order, has the user-visible
  /// effect of a reference execution when nothing outside the cohort can
  /// observe any of their writes in transition. Requires VPI off, invisible
  /// storage/nets without toggle coverage, and a closed transitive cone of
  /// transparent combinational observers for every written range.
  bool canReorder(llvm::ArrayRef<sim::SimFuncOp> functions,
                  CombinationalFusionFailure *failureReason = nullptr) const;
  /// Storage roots whose intermediate values no user-visible observer can
  /// see: invisible state whose every waiting process is a side-effect-free
  /// combinational activation (complete writes, final-output independence or
  /// a path-proven idempotent transition) writing only such roots. Greatest
  /// fixpoint, computed on first use. Net roots carry the activation-memory
  /// net tag.
  const llvm::DenseSet<uint64_t> &getGlitchInsensitiveStorage() const;
  /// The opaque observer that keeps `root` out of the glitch-insensitive
  /// storage, when one was recorded; diagnostics only.
  mlir::StringAttr getGlitchObserver(uint64_t root) const {
    return glitchObservers.lookup(root);
  }

private:
  bool unsupported = false;
  sim::SimDesignOp design;
  const analysis::HandleDataflowAnalysis *provenance = nullptr;
  mutable std::optional<llvm::DenseSet<uint64_t>> glitchInsensitive;
  /// For a sensitive root: the observer outside the closure that exposes it.
  mutable llvm::DenseMap<uint64_t, mlir::StringAttr> glitchObservers;
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
/// update. Talos performs every scheduled NBA of a region before the
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
  /// Immediate source-process writers, excluding declaration initialization.
  /// Absence is incomplete, rather than an empty writer set.
  std::optional<llvm::DenseSet<uint64_t>> immediateWriters;
};

/// Classify the design's storage, or std::nullopt when its observers cannot
/// be inventoried.
std::optional<NBATransientObservers>
computeNBATransientObservers(sim::SimDesignOp design, bool guardedVPI = false);

/// Return continuation targets that can coexist in the Active ready set when
/// the given sensitivity awakens. A constant-delay continuation is excluded
/// only when graph activation edges prove it is the unique producer currently
/// publishing the sensitivity; independent deadlines remain barriers.
mlir::SmallVector<uint32_t>
getComputeFusionReadyTargets(schedule::ComputeGraphAttr graph,
                             schedule::ComputeEffectAttr sensitivity);

} // namespace talos

#endif // TALOS_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H
