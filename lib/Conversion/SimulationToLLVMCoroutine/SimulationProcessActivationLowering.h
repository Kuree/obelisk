//===- SimulationProcessActivationLowering.h - Activation ABI -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace obelisk::detail {

struct NativeSchedulePlan {
  uint32_t initialRank = UINT32_MAX;
  llvm::SmallVector<std::pair<uint32_t, uint32_t>> continuations;
  llvm::SmallVector<uint32_t> bytecodeContinuations;
  std::optional<uint32_t> actorSlot;
};

mlir::LogicalResult
makeProcessActivationHelper(mlir::ModuleOp module, sim::SimFuncOp function,
                            const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult
makeProcessSpawnHelper(mlir::ModuleOp module, sim::SimFuncOp function,
                       const SimulationProcessFrameAnalysis &analysis,
                       const NativeSchedulePlan &schedule);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H
