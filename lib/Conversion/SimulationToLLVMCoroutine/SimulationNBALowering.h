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
};

void populateNBAToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                         mlir::TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         bool staticSitesEnabled,
                                         bool guardedClaims);
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
