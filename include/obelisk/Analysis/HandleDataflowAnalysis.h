//===- HandleDataflowAnalysis.h - SSA handles ---------------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_HANDLEDATAFLOWANALYSIS_H
#define OBELISK_ANALYSIS_HANDLEDATAFLOWANALYSIS_H

#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "llvm/ADT/DenseMap.h"
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
  bool operator==(const HandleCertificate &rhs) const {
    return constantAddress == rhs.constantAddress &&
           laneKnown == rhs.laneKnown && low == rhs.low && width == rhs.width &&
           stride == rhs.stride && clipped == rhs.clipped &&
           directDynamicSelection == rhs.directDynamicSelection;
  }
};
struct HandleDataflowResult {
  HandleFacts facts;
  llvm::DenseMap<mlir::Value, HandleCertificate> certificates;
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

private:
  llvm::DenseMap<uint64_t, uint64_t> driverNets;
};
HandleFacts deriveHandleFacts(sim::SimFuncOp function);
} // namespace obelisk::analysis
#endif
