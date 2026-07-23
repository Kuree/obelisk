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

  /// Fixed packed width of a logical net descriptor, when known.
  std::optional<uint64_t> getNetWidth(uint64_t net) const;

private:
  llvm::DenseMap<uint64_t, uint64_t> netBases;
  llvm::DenseMap<uint64_t, uint64_t> netWidths;
  mlir::SmallVector<uint64_t> parents;
  llvm::DenseMap<uint64_t, mlir::SmallVector<NetBit>> components;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_NETCONNECTIVITYANALYSIS_H
