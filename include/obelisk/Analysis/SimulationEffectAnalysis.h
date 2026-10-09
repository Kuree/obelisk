#ifndef OBELISK_ANALYSIS_SIMULATIONEFFECTANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONEFFECTANALYSIS_H

#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"

namespace obelisk::sim {
class SimFuncOp;
}
namespace obelisk::analysis {
/// A type holding only plain bits: integers, four-state logic, and packed or
/// fixed unpacked aggregates of them. It has no managed storage.
bool isPlainValueType(mlir::Type type);
/// A projection of a `simulation.ref.alloc` slot of a plain value type.
/// Accesses through it are private and have no runtime failure path.
bool isLocalSlotReference(mlir::Value reference);
/// The allocation a local slot projection selects from, or nullptr.
mlir::Operation *getLocalSlot(mlir::Value reference);

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
