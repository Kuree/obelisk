//===- NetConnectivityAnalysis.h - Static net topology facts ---*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H
#define OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace obelisk::sim {
class SimDesignOp;
}

namespace obelisk::analysis {

/// Disjoint strong contributions to isolated, undelayed plain wire nets.
/// Sparse intervals are sorted [low, high) ranges. They certify exclusivity
/// only within a contribution, never knownness or coverage of undriven bits.
///
/// Nets collapsed by `simulation.net.connect.decl` are merged into one
/// simulated net (IEEE 1800-2023 23.3.3.7), so the drivers of every member
/// are that net's contributions. `connectedExclusiveRanges` holds the bits of
/// such nets whose whole component is plain, undelayed wire with exactly one
/// strong contribution: Table 6-2 (6.6.1) resolves a lone contribution to its
/// own value, so a drive of them is a plain write to every member.
/// `connectedExclusiveAliases` names the other members of those bits.
struct IsolatedWireDriverTopology {
  using SparseRanges =
      llvm::DenseMap<uint64_t,
                     mlir::SmallVector<std::pair<uint64_t, uint64_t>>>;
  /// Bits [low, low + width) of the owning net are the same simulated bits as
  /// [memberLow, memberLow + width) of `member`, bit `low + i` matching
  /// `memberLow + i`, or `memberLow + width - 1 - i` when reversed.
  struct AliasRun {
    uint64_t low = 0, width = 0, member = 0, memberLow = 0,
             memberWidth = 0;
    bool reversed = false;
  };
  llvm::DenseSet<uint64_t> singleDriverNets;
  llvm::DenseSet<uint64_t> partitionedDriverNets;
  SparseRanges sparseDriverRanges;
  SparseRanges connectedExclusiveRanges;
  llvm::DenseMap<uint64_t, mlir::SmallVector<AliasRun>>
      connectedExclusiveAliases;
};

/// Memoized per design: the result is recomputed only when a net, driver,
/// connection or pass-switch declaration of the design changed. Analyses
/// constructed per function share one immutable snapshot.
std::shared_ptr<const IsolatedWireDriverTopology>
getIsolatedWireDriverTopology(sim::SimDesignOp design);

/// Plain, undelayed wire nets with one full-width strong driver, no collapsed
/// connections, user resolution or pass switches. Their resolved value is the
/// driver's value (IEEE 1800-2023 6.6.1). Shared by scheduling and lowering.
llvm::DenseSet<uint64_t> getIsolatedSingleDriverNets(sim::SimDesignOp design);

/// The same plain-wire topology, allowing strong drivers that partition the
/// net's bits without overlap or holes. Every bit still has exactly one
/// contributor; each source activation retains its own sensitivity.
llvm::DenseSet<uint64_t>
getIsolatedPartitionedDriverNets(sim::SimDesignOp design);

struct NetBit {
  uint64_t net = 0;
  uint64_t offset = 0;

  bool operator==(const NetBit &other) const {
    return net == other.net && offset == other.offset;
  }
  bool operator<(const NetBit &other) const {
    return net < other.net || (net == other.net && offset < other.offset);
  }
};

enum class NetDominanceKind {
  /// The bit is not connected through a port and therefore dominates itself.
  Isolated,
  /// Every directed collapse edge leads to one dominating component member.
  Unique,
  /// At least one collapse edge predates or omits dominance direction.
  Incomplete,
  /// Directed edges have multiple sinks or contain a dominance cycle.
  Ambiguous,
};

struct NetDominance {
  NetDominanceKind kind = NetDominanceKind::Isolated;
  NetBit bit;
};

/// Immutable topology derived only from `simulation.net.connect.decl`.
/// Keeping this separate from SimulationAnalysis lets concurrent IPO retain
/// its existing cache and invalidation contract.
class NetConnectivityAnalysis {
public:
  explicit NetConnectivityAnalysis(sim::SimDesignOp design);

  /// Every logical bit equivalent to `bit`, sorted by net ID then offset.
  mlir::ArrayRef<NetBit> getComponent(NetBit bit) const;

  /// Canonical representative of a logical bit. Unconnected bits represent
  /// themselves.
  NetBit getCanonical(NetBit bit) const;

  /// LRM 23.3.3.7 dominating member for the simulated-net component.
  NetDominance getDominance(NetBit bit) const;

  /// Sink members of an acyclic, completely directed collapse component.
  /// Multiple sinks can still determine one effective net type when all have
  /// the same resolution category.
  mlir::ArrayRef<NetBit> getDominatingBits(NetBit bit) const;

  /// Logical bits named by at least one static connection, sorted by net and
  /// offset. Isolated bits remain implicit in the sparse topology.
  mlir::ArrayRef<NetBit> getConnectedBits() const { return connectedBits; }

  /// Fixed simulation-storage width of a logical net descriptor, when known.
  std::optional<uint64_t> getNetWidth(uint64_t net) const;

private:
  llvm::DenseMap<uint64_t, uint64_t> netBases;
  llvm::DenseMap<uint64_t, uint64_t> netWidths;
  llvm::DenseMap<uint64_t, uint64_t> parents;
  mlir::SmallVector<NetBit> connectedBits;
  llvm::DenseMap<uint64_t, mlir::SmallVector<NetBit>> components;
  llvm::DenseMap<uint64_t, NetDominance> dominance;
  llvm::DenseMap<uint64_t, mlir::SmallVector<NetBit>> dominatingBits;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H
