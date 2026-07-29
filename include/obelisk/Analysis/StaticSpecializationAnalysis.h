//===- StaticSpecializationAnalysis.h - Validate static plans ---*- C++ -*-===//
//
// A shared, read-only view of the revision-coupled static state and NBA plan.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_STATICSPECIALIZATIONANALYSIS_H
#define OBELISK_ANALYSIS_STATICSPECIALIZATIONANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <cstdint>

namespace obelisk::analysis {

/// Validates static-specialization metadata against its source design and
/// derives the NBA site inventory consumed by both native and bytecode
/// lowering.
///
/// The analyzed design must remain alive and unchanged for this object's
/// lifetime. Recompute the analysis after mutating the design or its graph.
class StaticSpecializationAnalysis {
public:
  static mlir::FailureOr<StaticSpecializationAnalysis>
  compute(sim::SimDesignOp design);

  explicit operator bool() const { return plan != nullptr; }
  sim::StaticSpecializationAttr getPlan() const { return plan; }

  const llvm::DenseMap<uint64_t, sim::StaticStateRootAttr> &getRoots() const {
    return roots;
  }
  mlir::ArrayRef<uint64_t> getNBARoots() const { return nbaRoots; }
  const llvm::DenseSet<uint64_t> &getNBASites() const { return nbaSites; }
  mlir::ArrayRef<sim::ComputeNBACommitAttr> getOrderedNBACommits() const {
    return orderedNBACommits;
  }

private:
  sim::StaticSpecializationAttr plan;
  llvm::DenseMap<uint64_t, sim::StaticStateRootAttr> roots;
  mlir::SmallVector<uint64_t> nbaRoots;
  llvm::DenseSet<uint64_t> nbaSites;
  mlir::SmallVector<sim::ComputeNBACommitAttr> orderedNBACommits;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_STATICSPECIALIZATIONANALYSIS_H
