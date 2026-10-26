//===- HandleDataflowAnalysis.h - SSA handles ---------------------*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_HANDLEDATAFLOWANALYSIS_H
#define OBELISK_ANALYSIS_HANDLEDATAFLOWANALYSIS_H

#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <optional>

namespace obelisk::sim {
class SimDesignOp;
class SimFuncOp;
} // namespace obelisk::sim
namespace obelisk::analysis {

/// Root/range projection of the reference lattice used by effect consumers.
/// Unknown identities never become concrete descriptors. Dynamic ranges
/// describe possible selections and cannot certify a constant address.
struct HandleFact {
  schedule::ComputeResourceKind resource =
      schedule::ComputeResourceKind::Unknown;
  std::optional<uint64_t> descriptor;
  std::optional<unsigned> formal;
  uint64_t low = 0, width = 0, rootWidth = 0;
  bool dynamic = false;
  bool operator==(const HandleFact &rhs) const {
    return resource == rhs.resource && descriptor == rhs.descriptor &&
           formal == rhs.formal && low == rhs.low && width == rhs.width &&
           rootWidth == rhs.rootWidth && dynamic == rhs.dynamic;
  }
};
using HandleFacts = llvm::DenseMap<mlir::Value, HandleFact>;

/// Additional lowering facts solved in the same lattice as root identity.
/// A metadata capture can have a known root without being materialized as a
/// constant handle. Lane facts retain strided array-field geometry; unequal
/// lane joins forget geometry. Dynamic selectors retain validity/clipping
/// guards in lowering (IEEE 1800-2023 11.5.1-2).
struct HandleCertificate {
  bool constantAddress = false;
  bool laneKnown = false;
  uint64_t low = 0, width = 0, stride = 0;
  bool clipped = false;
  mlir::Value directDynamicSelection;
  bool inBounds = false;
  /// Every selection is valid once each of `knownSelectors` has no X/Z bits:
  /// all values those selectors can take when known lie in range. `inBounds`
  /// implies this with no selectors. An X/Z selector makes the selection
  /// invalid (IEEE 1800-2023 7.4.6, 11.5.1), so only consumers that also
  /// prove the selectors known may treat the access as valid.
  bool inBoundsIfKnown = false;
  llvm::SmallVector<mlir::Value, 2> knownSelectors;
  bool operator==(const HandleCertificate &rhs) const {
    return constantAddress == rhs.constantAddress &&
           laneKnown == rhs.laneKnown && low == rhs.low && width == rhs.width &&
           stride == rhs.stride && clipped == rhs.clipped &&
           directDynamicSelection == rhs.directDynamicSelection &&
           inBounds == rhs.inBounds && inBoundsIfKnown == rhs.inBoundsIfKnown &&
           knownSelectors == rhs.knownSelectors;
  }
};
struct HandleDataflowResult {
  HandleFacts facts;
  llvm::DenseMap<mlir::Value, HandleCertificate> certificates;
  /// Scalar proofs collected with selector analysis. These values have a
  /// zero unknown plane on every execution, including all admitted calls.
  /// Consumers may elide the internal plane without weakening the ABI.
  llvm::DenseSet<mlir::Value> knownLogicValues;
  /// Only blocks proved dead by the solver are excluded. Missing facts or a
  /// failed analysis never constitute a proof that a block cannot execute.
  llvm::DenseSet<mlir::Block *> nonExecutableBlocks;
  bool isExecutable(mlir::Block *block) const {
    return !nonExecutableBlocks.contains(block);
  }
};

/// MLIR sparse forward dataflow over SSA, CFG and region interfaces. The
/// lattice is bottom -> known root/range/lane -> known root with dynamic
/// selection -> unknown identity. No hand-written whole-function fixpoint or
/// recursive predecessor walk remains. Driver normalization is indexed once
/// per design before function analyses run concurrently; analysis snapshots
/// are recomputed after IR changes.
class HandleDataflowAnalysis {
public:
  explicit HandleDataflowAnalysis(sim::SimDesignOp design);
  /// Avoid indexing the design for a single function without driver handles.
  explicit HandleDataflowAnalysis(sim::SimFuncOp function);
  HandleDataflowResult analyze(sim::SimFuncOp function) const;
  HandleFacts derive(sim::SimFuncOp function) const;
  bool hasIsolatedSingleDriver(uint64_t net) const {
    return drivers->singleDriverNets.contains(net);
  }
  /// The bits [low, low + width) of `net` have exactly one strong, undelayed
  /// contribution, so a drive of them is a plain write of the resolved value
  /// (IEEE 1800-2023 6.6.1, Table 6-2): the isolated net has one driver, its
  /// drivers partition it, the slice lies within one sparse contribution, or
  /// the net is merged with port-connected members into one simulated net
  /// (23.3.3.7) that has one contribution for each of these bits. Undriven
  /// bits of a sparse net stay z and are never claimed.
  bool hasExclusiveDriverSlice(uint64_t net, uint64_t low,
                               uint64_t width) const {
    if (drivers->partitionedDriverNets.contains(net))
      return true;
    if (!width || low > UINT64_MAX - width)
      return false;
    auto within = [&](const auto &ranges) {
      return llvm::any_of(ranges, [&](auto range) {
        return low >= range.first && low + width <= range.second;
      });
    };
    auto sparse = drivers->sparseDriverRanges.find(net);
    if (sparse != drivers->sparseDriverRanges.end() && within(sparse->second))
      return true;
    auto connected = drivers->connectedExclusiveRanges.find(net);
    return connected != drivers->connectedExclusiveRanges.end() &&
           within(connected->second);
  }
  /// One other member of a collapsed net that a drive of an exclusive slice
  /// updates: bits [low, low + width) of `net`, whose declaration spans
  /// `rootWidth` bits; bit `low + i` receives driven bit `drivenLow + i`, or
  /// driven bit `drivenLow + width - 1 - i` when reversed.
  struct DriveAlias {
    uint64_t net = 0, low = 0, width = 0, rootWidth = 0, drivenLow = 0;
    bool reversed = false;
  };
  /// The members of every collapsed component that a drive of the exclusive
  /// slice [low, low + width) of `net` writes besides `net` itself, sorted by
  /// position in the slice. Empty for an isolated net or an unproved slice.
  llvm::SmallVector<DriveAlias>
  getExclusiveDriveAliases(uint64_t net, uint64_t low, uint64_t width) const;

private:
  llvm::DenseMap<uint64_t, uint64_t> driverNets;
  /// Never null: a design-less analysis shares an empty topology.
  std::shared_ptr<const IsolatedWireDriverTopology> drivers;
  /// Order-independent digest of `driverNets`, part of each memoized
  /// function revision.
  uint64_t driverDigest = 0;
};
HandleFacts deriveHandleFacts(sim::SimFuncOp function);
} // namespace obelisk::analysis
#endif
