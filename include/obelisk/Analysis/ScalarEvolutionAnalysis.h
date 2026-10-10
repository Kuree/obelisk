//===- ScalarEvolutionAnalysis.h - Integer recurrence proofs -----*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_SCALAREVOLUTIONANALYSIS_H
#define OBELISK_ANALYSIS_SCALAREVOLUTIONANALYSIS_H

#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <memory>
#include <optional>

namespace obelisk::analysis {
/// Function-owned LLVM scalar evolution over a disposable, integer-only CFG.
/// Only selector dependencies and control predicates are translated. Opaque
/// producers stay nondeterministic at their original program point; unknown
/// four-state values are never treated as ordinary integer expressions.
/// No context, IR, solver or mutable cache is shared between MLIR workers.
class ScalarEvolutionAnalysis {
public:
  ScalarEvolutionAnalysis(mlir::Operation *function,
                          const llvm::DenseSet<mlir::Value> &selectors);
  ~ScalarEvolutionAnalysis();
  std::optional<mlir::ConstantIntRanges> getRange(mlir::Value value) const;
  bool excludesXZ(mlir::Value value) const;
  /// A bounded SMT fallback for a selector not discharged by range inference.
  /// The callback supplies facts at the access, not facts from another edge.
  bool proveBounds(
      mlir::Value value, const llvm::APInt &lower, const llvm::APInt &upper,
      llvm::function_ref<std::optional<mlir::ConstantIntRanges>(mlir::Value)>
          scopedRange) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace obelisk::analysis
#endif
