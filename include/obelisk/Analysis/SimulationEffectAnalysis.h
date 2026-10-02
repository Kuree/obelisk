#ifndef OBELISK_ANALYSIS_SIMULATIONEFFECTANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONEFFECTANALYSIS_H

#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"

namespace obelisk::sim {
class SimFuncOp;
}
namespace obelisk::analysis {
/// Call-graph data flow, including recursive SCCs and unresolved targets.
/// A certificate covers the complete executable closure, never just a body.
class SimulationEffectAnalysis {
public:
  enum Effect : unsigned {
    Write = 1,
    Terminate = 2,
    External = 4,
    Suspend = 8,
    Unknown = 16
  };
  explicit SimulationEffectAnalysis(mlir::Operation *root);
  unsigned get(sim::SimFuncOp function) const;
  bool isReadOnly(sim::SimFuncOp function) const;
  bool isHarmless(sim::SimFuncOp function) const;

private:
  llvm::DenseMap<mlir::Operation *, unsigned> effects;
};
} // namespace obelisk::analysis
#endif
