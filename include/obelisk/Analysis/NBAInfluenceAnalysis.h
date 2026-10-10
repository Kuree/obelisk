//===- NBAInfluenceAnalysis.h - NBA influence cones ---------------*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_NBAINFLUENCEANALYSIS_H
#define OBELISK_ANALYSIS_NBAINFLUENCEANALYSIS_H

#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk::sim {
class SimDesignOp;
}
namespace obelisk::analysis {

/// Sparse backward dataflow on storage/net roots. The two-point may-observe
/// lattice propagates from observers through immediate read/watch ->
/// write/drive dependencies until a fixed point, including cycles. Effect
/// summaries include callee effects and handle provenance; unresolved effects
/// make the result top. NBA writes stop propagation at their capture/commit
/// boundary. This analysis only rejects candidates: per-root merge safety and
/// direct activation ownership remain separate prerequisites. Root-wide facts
/// intentionally overapproximate slices and control dependencies; lexical
/// privacy proves nothing here.
class NBAInfluenceAnalysis {
public:
  using Root = std::pair<schedule::ComputeResourceKind, uint64_t>;
  NBAInfluenceAnalysis(sim::SimDesignOp design, llvm::ArrayRef<Root> observers,
                       bool guardedVPI = false);
  bool mayReachObserver(Root root) const {
    return !complete || !known.contains(root) || observed.contains(root);
  }
  /// Stronger than private influence: no process can ever read/watch this
  /// root. Unknown effects reject the proof, including unresolved call reads.
  /// The caller must also exclude external/toggle observation.
  bool isNeverRead(Root root) const {
    return complete && known.contains(root) && !readers.contains(root);
  }

private:
  bool complete = false;
  llvm::DenseSet<Root> known, observed, readers;
};
} // namespace obelisk::analysis
#endif
