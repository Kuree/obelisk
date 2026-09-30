//===- ComputeFusion.h - Static process-body fusion helpers -----*- C++ -*-===//

#ifndef OBELISK_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H
#define OBELISK_LIB_DIALECT_SIMULATION_TRANSFORMS_COMPUTEFUSION_H

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk {

/// A finite, idempotent combinational activation. Every read is watched and
/// every written range is exclusive, disjoint and never read by this actor.
/// This is deliberately not an always_latch or arbitrary always certificate.
struct CombinationalFusionBody {
  mlir::Block *activation;
  mlir::Operation *suspend;
  mlir::SmallVector<mlir::Block *> blocks;
  mlir::SmallVector<mlir::Value> outputs;
};

/// Immutable, revision-local writer inventory for local ranked kernels.
class CombinationalFusionAnalysis {
public:
  CombinationalFusionAnalysis(
      sim::SimDesignOp design,
      const analysis::HandleDataflowAnalysis &provenance);
  std::optional<CombinationalFusionBody>
  analyze(sim::SimFuncOp function,
          const analysis::HandleDataflowAnalysis &provenance) const;

private:
  bool unsupported = false;
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
