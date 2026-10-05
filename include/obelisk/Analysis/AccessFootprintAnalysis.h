//===- AccessFootprintAnalysis.h - Footprints --------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_ACCESSFOOTPRINTANALYSIS_H
#define OBELISK_ANALYSIS_ACCESSFOOTPRINTANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "llvm/ADT/SmallVector.h"

namespace obelisk::analysis {

/// An access-local may footprint, in bits relative to the canonical root.
/// The sparse handle analysis supplies root identity and validity; a bounded
/// disjunctive dense analysis retains correlations between integer selectors
/// along executable CFG edges. Joins union paths. Widening and unsupported
/// transfers lose precision, never omit a possible access.
///
/// This is a read-only snapshot. Recompute after changing the function. A
/// missing footprint means unknown, not empty. It does not establish a global
/// known-state invariant or permit clearing any canonical X/Z bits.
class AccessFootprintAnalysis {
public:
  using Range = std::pair<uint64_t, uint64_t>; // low, width
  using Footprint = llvm::SmallVector<Range, 4>;

  AccessFootprintAnalysis(sim::SimFuncOp function,
                          const HandleDataflowResult &handles);
  const Footprint *lookup(mlir::Operation *access, mlir::Value reference) const;

private:
  llvm::DenseMap<std::pair<mlir::Operation *, mlir::Value>, Footprint>
      footprints;
};

} // namespace obelisk::analysis
#endif
