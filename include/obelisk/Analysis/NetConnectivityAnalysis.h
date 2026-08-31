//===- NetConnectivityAnalysis.h - Static net topology facts ---*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H
#define OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <optional>

namespace obelisk::analysis {

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

/// Immutable topology derived only from `obelisk_sim.net.connect.decl`.
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
