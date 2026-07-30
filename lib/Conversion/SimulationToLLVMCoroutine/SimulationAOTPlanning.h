//===- SimulationAOTPlanning.h - Native AOT plan support -------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H

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

mlir::FailureOr<llvm::SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots);
mlir::FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    bool enabled);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
