//===- SimulationAOTPlanning.h - Native AOT plan support -------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H

#include "SimulationNBALowering.h"

#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace obelisk::detail {

struct NativeStateLayout;

struct NativeStaticFanoutPlan {
  llvm::SmallVector<obelisk_rt_static_fanout_entry> entries;
  bool exact = false;
};

mlir::LogicalResult specializeNativeAOTCaptures(
    mlir::ModuleOp module,
    const analysis::NativeAOTAnalysis &eligibility);
mlir::FailureOr<llvm::SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots);
mlir::FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    bool enabled);
mlir::LogicalResult makeNativeAOTPlan(
    mlir::ModuleOp module, uint32_t actorCount,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    bool enableDirectState, bool enableStaticNBA, bool enableStaticControl,
    bool enableStaticFanout, bool enableCleanSuperstep, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
