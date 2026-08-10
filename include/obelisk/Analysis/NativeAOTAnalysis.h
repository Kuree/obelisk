//===- NativeAOTAnalysis.h - Native scheduler eligibility ------*- C++ -*-===//
//
// Read-only whole-module analysis for native AOT scheduler eligibility.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H
#define OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H

#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <string>

namespace obelisk::analysis {

/// Immutable native-scheduler eligibility facts for one module.
///
/// `eligible` means at least one statically bound actor can use native AOT
/// scheduling. `fullyEligible` additionally means the complete design avoids
/// every recorded generic or bytecode boundary.
class NativeAOTAnalysis {
public:
  NativeAOTAnalysis() = default;

  static NativeAOTAnalysis compute(mlir::ModuleOp module);

  bool isEligible() const { return eligible; }
  bool isFullyEligible() const { return fullyEligible; }
  /// True when every non-AOT boundary is compiler-generated cold concurrent
  /// assertion work: detached report callbacks or asynchronous disable
  /// observers. Explicit AOT may use the hybrid coordinator for these actors
  /// while retaining the statically bound monitor actors.
  bool isForcedHybridEligible() const { return forcedHybridEligible; }
  bool isAOTCostEffective() const { return aotCostEffective; }
  bool hasPeriodicClockCandidate() const { return periodicClockCandidate; }
  uint64_t getTotalGraphCost() const { return totalGraphCost; }
  uint64_t getNativeGraphCost() const { return nativeGraphCost; }
  mlir::ArrayRef<std::string> getReasons() const { return reasons; }

  const llvm::DenseMap<mlir::Operation *, uint32_t> &getActorSlots() const {
    return actorSlots;
  }
  const llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Block *>> &
  getBytecodeFragments() const {
    return bytecodeFragments;
  }

private:
  bool eligible = false;
  bool fullyEligible = false;
  bool forcedHybridEligible = false;
  bool aotCostEffective = false;
  bool periodicClockCandidate = false;
  uint64_t totalGraphCost = 0;
  uint64_t nativeGraphCost = 0;
  mlir::SmallVector<std::string> reasons;
  llvm::DenseMap<mlir::Operation *, uint32_t> actorSlots;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Block *>>
      bytecodeFragments;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_NATIVEAOTANALYSIS_H
