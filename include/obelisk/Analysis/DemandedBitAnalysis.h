//===- DemandedBitAnalysis.h - Observable packed bit demands -------*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_DEMANDEDBITANALYSIS_H
#define OBELISK_ANALYSIS_DEMANDEDBITANALYSIS_H
#include "mlir/IR/Value.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
namespace obelisk::analysis {
/// Sparse backward dataflow: bit masks union at users and CFG joins. Unknown
/// operations, calls and effects demand the full input, including its X/Z
/// plane. Masks describe both planes, never knownness or value equality.
class DemandedBitAnalysis {
public:
  explicit DemandedBitAnalysis(mlir::Operation *function);
  const llvm::APInt *get(mlir::Value value) const {
    auto found = demands.find(value);
    return found == demands.end() ? nullptr : &found->second;
  }

private:
  llvm::DenseMap<mlir::Value, llvm::APInt> demands;
};
} // namespace obelisk::analysis
#endif
