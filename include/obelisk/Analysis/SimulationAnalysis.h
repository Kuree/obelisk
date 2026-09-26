//===- SimulationAnalysis.h - Simulation analysis utilities ----*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_SIMULATIONANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationEnums.h"

#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <optional>

namespace obelisk::sim {
class SimDesignOp;
class SimFuncOp;
}

namespace obelisk::analysis {

/// Concrete descriptor provenance recomputed from executable SSA and CFG.
/// Absence from a map means no fact has reached the value; a present unknown
/// fact means analysis proved that the value cannot retain concrete identity.
struct DescriptorProvenance {
  sim::ComputeResourceKind resource = sim::ComputeResourceKind::Unknown;
  std::optional<uint64_t> descriptor;
  std::optional<unsigned> formal;
  uint64_t low = 0;
  uint64_t width = 0;
  uint64_t rootWidth = 0;
  bool dynamic = false;

  bool operator==(const DescriptorProvenance &other) const {
    return resource == other.resource && descriptor == other.descriptor &&
           formal == other.formal && low == other.low && width == other.width &&
           rootWidth == other.rootWidth && dynamic == other.dynamic;
  }
};

using DescriptorProvenanceMap =
    llvm::DenseMap<mlir::Value, DescriptorProvenance>;

/// Immutable design-wide descriptor lookup shared by provenance queries.
/// Construct one when deriving provenance for multiple functions so driver
/// normalization does not rescan the design for every function.
class DescriptorProvenanceAnalysis {
public:
  explicit DescriptorProvenanceAnalysis(sim::SimDesignOp design);

  DescriptorProvenanceMap derive(sim::SimFuncOp function) const;

private:
  llvm::DenseMap<uint64_t, uint64_t> driverNets;
};

/// Which NBA commit roots may merge several updates between two NBA barriers
/// into one old-to-final transition. IEEE 1800-2017 4.6(b) performs each NBA
/// in execution order and 9.4.2 detects an event on each resulting update, so
/// a merge is sound only for a root whose intermediate values nothing can
/// observe. The design's sim::metadata::nbaTransientObservable records the
/// observable roots; without it every root is treated as observable.
class NBAMergeSafety {
public:
  explicit NBAMergeSafety(sim::SimDesignOp design);
  /// Nothing observes an intermediate value: any merge, including a
  /// compile-time fold that drops the earlier write, is exact.
  bool storageMayMerge(uint64_t descriptor) const {
    return known && !observable.contains(descriptor) &&
           !changeWatched.contains(descriptor);
  }
  /// Only waits for any change observe the root. A merge is exact when it
  /// records bits rewritten with a different value (the transient mask) and
  /// the commit reports them as changed.
  bool storageNeedsTransients(uint64_t descriptor) const {
    return known && changeWatched.contains(descriptor);
  }
  bool commitMayMerge(uint32_t commit) const {
    auto storage = commitStorage.find(commit);
    return storage != commitStorage.end() && storageMayMerge(storage->second);
  }
  bool commitNeedsTransients(uint32_t commit) const {
    auto storage = commitStorage.find(commit);
    return storage != commitStorage.end() &&
           storageNeedsTransients(storage->second);
  }

private:
  bool known = false;
  llvm::DenseSet<uint64_t> observable;
  llvm::DenseSet<uint64_t> changeWatched;
  llvm::DenseMap<uint32_t, uint64_t> commitStorage;
};

/// Physical bit width used by the canonical simulation state and process
/// frame representations. This includes fixed unpacked aggregates and the tag
/// carried beside an unpacked tagged-union payload.
std::optional<unsigned> getSimulationStorageBitWidth(mlir::Type type);

/// Whether a type contains four-state logic in canonical simulation storage.
/// Managed handles are opaque two-state words even when their pointee types
/// contain logic.
bool containsFourStateLogic(mlir::Type type);

/// Derive stable descriptor roots and ranges for all handle-typed values in a
/// defined simulation function. Driver handles are normalized to their net.
/// Prefer DescriptorProvenanceAnalysis when querying multiple functions.
DescriptorProvenanceMap deriveDescriptorProvenance(sim::SimFuncOp function);

/// Weighted cost shared by IPO growth accounting and compute-graph lane
/// balancing. Terminators are free, ordinary operations cost one, state access
/// costs three, and a remaining direct call costs five.
uint64_t getSimulationOperationCost(mlir::Operation &operation);

/// Sum weighted operation cost recursively below an operation or region.
uint64_t getSimulationOperationCost(mlir::Operation *operation);
uint64_t getSimulationRegionCost(mlir::Region &region);

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_SIMULATIONANALYSIS_H
