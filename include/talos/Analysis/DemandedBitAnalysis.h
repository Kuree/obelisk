//===- DemandedBitAnalysis.h - Observable packed bit demands -------*- C++
//-*-===//
#ifndef TALOS_ANALYSIS_DEMANDEDBITANALYSIS_H
#define TALOS_ANALYSIS_DEMANDEDBITANALYSIS_H
#include "mlir/IR/Value.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
namespace mlir {
class SymbolTableCollection;
} // namespace mlir
namespace talos::analysis {
/// Sparse backward dataflow: bit masks union at users and CFG joins. Unknown
/// operations, calls and effects demand the full input, including its X/Z
/// plane. Masks describe both planes, never knownness or value equality.
class DemandedBitAnalysis {
public:
  explicit DemandedBitAnalysis(mlir::Operation *function);
  /// Resolves call targets through `symbols`, which callers analyzing many
  /// functions of an unchanging symbol scope share.
  DemandedBitAnalysis(mlir::Operation *function,
                      mlir::SymbolTableCollection &symbols);
  const llvm::APInt *get(mlir::Value value) const {
    auto found = demands.find(value);
    return found == demands.end() ? nullptr : &found->second;
  }

private:
  llvm::DenseMap<mlir::Value, llvm::APInt> demands;
};
} // namespace talos::analysis
#endif
