//===- SimulationNBALowering.h - Native NBA lowering support ----*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace mlir {
class TypeConverter;
}

namespace obelisk::detail {

struct NativeStateLayout;

struct NativeStaticNBAPlan {
  llvm::SmallVector<obelisk_rt_static_nba_root> roots;
  llvm::SmallVector<obelisk_rt_static_nba_site> sites;
  llvm::DenseMap<uint64_t, uint32_t> siteRoots;
  llvm::SmallVector<std::string> generatedAccumulators;
  // Canonical state-plane bit offset for each root. This is revision-coupled
  // lowering metadata, not a second state allocation.
  llvm::SmallVector<uint64_t> generatedOffsets;
  // Fixed NBA event region for roots whose every reachable site is a direct
  // scalar stage in the same region. UINT32_MAX denotes a mixed or unsupported
  // root. Region and full-root coverage are separate proofs: fixed partial
  // writes still require a write mask but not valid/region bookkeeping.
  llvm::SmallVector<uint32_t> generatedCommitRegions;
  llvm::SmallVector<bool> generatedFullRootStages;
  // Nonzero when every reachable direct scalar enqueue writes the same fixed
  // root mask. The accumulator value can then be overwritten instead of
  // read-modify-written; repeated activations retain normal last-write wins.
  llvm::SmallVector<uint64_t> generatedFixedWriteMasks;
};

void populateNBAToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                         mlir::TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         bool staticSitesEnabled,
                                         bool guardedClaims,
                                         bool evalCeiling);
mlir::FailureOr<NativeStaticNBAPlan> buildNativeStaticNBAPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    mlir::ArrayRef<sim::ComputeNBACommitAttr> orderedCommits, bool enabled);
mlir::LogicalResult
materializeGeneratedNBAAccumulators(mlir::ModuleOp module,
                                    const NativeStaticNBAPlan &plan);
mlir::LogicalResult markCleanStaticNBAsInGuardedBodies(
    mlir::ModuleOp module, bool enabled,
    const llvm::DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    mlir::ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
