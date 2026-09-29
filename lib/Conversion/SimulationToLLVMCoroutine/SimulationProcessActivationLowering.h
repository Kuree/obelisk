//===- SimulationProcessActivationLowering.h - Activation ABI -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

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
makeProcessActivationHelper(mlir::ModuleOp module, mlir::SymbolTable &symbols,
                            sim::SimFuncOp function,
                            const SimulationProcessFrameAnalysis &analysis);
mlir::FailureOr<mlir::LLVM::LLVMFuncOp> makeProcessSpawnHelper(
    mlir::ModuleOp module, mlir::SymbolTable &symbols, sim::SimFuncOp function,
    const SimulationProcessFrameAnalysis &analysis,
    const NativeSchedulePlan &schedule, bool materializeBody = true);

mlir::LogicalResult
makeProcessSpawnBody(mlir::LLVM::LLVMFuncOp helper,
                     const SimulationProcessFrameAnalysis &analysis);

void declareProcessSpawnRuntimeABI(mlir::ModuleOp module);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PROCESS_ACTIVATION_LOWERING_H
