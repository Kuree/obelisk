//===- SimulationNBAPlanning.cpp - Native static NBA plan support -------===//

#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationNBALowering.h"
#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <limits>

using namespace mlir;

#include "../../../Conversion/SimulationToSchedule/NativeNBAUtils.h"
namespace obelisk::detail {
LogicalResult markCleanStaticNBAsInGuardedBodies(
    ModuleOp module, bool enabled,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    if (::obelisk::schedule::has<
            schedule::metadata::nativeGuardedSpecializationBody>(function))
      functions.push_back(function);
  });

  for (sim::SimFuncOp function : functions) {
    ::obelisk::schedule::remove<
        schedule::metadata::nativeGuardedSpecializationBody>(function);
    if (!enabled)
      continue;

    Operation *suspension = nullptr;
    bool multipleSuspensions = false;
    function.walk([&](Operation *operation) {
      if (!sim::isSuspensionOp(operation))
        return;
      multipleSuspensions |= suspension != nullptr;
      suspension = operation;
    });
    if (multipleSuspensions || !suspension ||
        suspension->getNumSuccessors() != 1)
      return function.emitOpError(
                 "has invalid guarded-specialization activation structure"),
             failure();

    Block *activationEntry = suspension->getSuccessor(0);
    if (activationEntry == &function.getBody().front() ||
        activationEntry->getParent() != &function.getBody())
      return function.emitOpError(
                 "has invalid guarded-specialization continuation"),
             failure();

    // Runtime dispatch selects native or bytecode execution at this activation
    // boundary. The native body is the clean form; dirty actors never enter it.
    SmallVector<Block *> activationBlocks;
    SmallVector<Block *> pending{activationEntry};
    llvm::SmallPtrSet<Block *, 16> visited;
    Block *suspensionBlock = suspension->getBlock();
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == suspensionBlock || !visited.insert(block).second)
        continue;
      if (block->getParent() != &function.getBody())
        return function.emitOpError(
                   "guarded-specialization body leaves its process region"),
               failure();
      if (llvm::any_of(*block, [](Operation &operation) {
            return sim::isSuspensionOp(&operation);
          }))
        return function.emitOpError(
                   "guarded-specialization body contains a suspension"),
               failure();
      activationBlocks.push_back(block);
      for (Block *successor : block->getSuccessors())
        if (successor != suspensionBlock)
          pending.push_back(successor);
    }
    if (activationBlocks.empty())
      return function.emitOpError("has an empty guarded-specialization body"),
             failure();

    // A generic enqueue claims its root's slow path for the rest of the slot.
    // Elide per-site guards only when every reachable enqueue is statically
    // staged and cannot invalidate that invariant mid-activation.
    bool nbaActivationIsNonInvalidating = true;
    for (Block *block : activationBlocks)
      block->walk([&](sim::SimNBAEnqueueOp nba) {
        nbaActivationIsNonInvalidating &= isNonInvalidatingStaticNBA(
            nba, staticNBASiteRoots, staticNBARoots, stateLayout);
      });

    for (Block *source : activationBlocks)
      for (Operation &operation : *source)
        operation.walk([&](Operation *nested) {
          if (nbaActivationIsNonInvalidating &&
              isa<sim::SimNBAEnqueueOp>(nested))
            ::obelisk::schedule::set<assumeCleanSpecializationAttr>(
                nested, UnitAttr::get(function.getContext()));
        });
  }
  return success();
}

} // namespace obelisk::detail
