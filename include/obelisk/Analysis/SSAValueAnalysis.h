//===- SSAValueAnalysis.h - Shared forwarding facts -------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_SSAVALUEANALYSIS_H
#define OBELISK_ANALYSIS_SSAVALUEANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk::analysis {

/// Immutable function snapshots. Rebuild after changing CFG operands.
class SemanticValueRootAnalysis {
public:
  explicit SemanticValueRootAnalysis(sim::SimFuncOp function);
  mlir::Value lookup(mlir::Value value) const {
    auto found = roots.find(value);
    return found == roots.end() ? value : found->second;
  }

private:
  llvm::DenseMap<mlir::Value, mlir::Value> roots;
};

class ConstantTimeAnalysis {
public:
  explicit ConstantTimeAnalysis(sim::SimFuncOp function);
  bool isConstant(mlir::Value value) const { return constants.contains(value); }

private:
  llvm::DenseSet<mlir::Value> constants;
};

} // namespace obelisk::analysis
#endif
