//===- SimulationProcessWrapperLowering.h - Native process wrappers ---===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"

namespace obelisk::detail {

inline constexpr llvm::StringLiteral nativeCoroutineExecuteName =
    "__obelisk_native_coro_execute_v1";
inline constexpr llvm::StringLiteral nativeCoroutineDestroyName =
    "__obelisk_native_coro_destroy_v1";
inline constexpr llvm::StringLiteral nativeNoopDestroyName =
    "__obelisk_native_noop_destroy_v1";
inline constexpr llvm::StringLiteral nativeZeroRequirementsName =
    "__obelisk_native_zero_requirements_v1";

mlir::LogicalResult materializeSharedNativeWrappers(mlir::ModuleOp module,
                                                    bool needsCoroutine);

void publishAction(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value instance, uint32_t actionKind,
                   uint32_t suspendKind, uint32_t continuation, uint32_t flags,
                   mlir::Value payload, uint64_t auxiliary);
mlir::LogicalResult makeNativeWrappers(mlir::ModuleOp module,
                                       mlir::LLVM::LLVMFuncOp ramp,
                                       llvm::StringRef baseName,
                                       bool directActivation = false);
mlir::LogicalResult
makePlainNativeWrappers(mlir::ModuleOp module, mlir::func::FuncOp body,
                        llvm::StringRef baseName,
                        const SimulationProcessFrameAnalysis &analysis);
struct DeferredDirectFragmentWrapper {
  mlir::LLVM::LLVMFuncOp wrapper;
  sim::SimFuncOp body;
  sim::SimFuncOp actor;
  uint32_t actorSlot;
  uint32_t continuation;
  const SimulationProcessFrameAnalysis *analysis;
};

/// Termination facts shared by the wrappers of one direct-fragment
/// materialization: each function is scanned once and callees resolve through
/// the design's symbol table. Valid while neither the scanned functions nor the
/// design's symbols change.
struct DirectFragmentTerminationCache {
  explicit DirectFragmentTerminationCache(mlir::SymbolTable &designSymbols)
      : designSymbols(designSymbols) {}
  struct Facts {
    bool mayTerminate = false;
    llvm::SmallVector<sim::SimFuncOp> callees;
  };
  mlir::SymbolTable &designSymbols;
  llvm::DenseMap<mlir::Operation *, Facts> facts;
};

mlir::FailureOr<mlir::LLVM::LLVMFuncOp>
makeDirectFragmentWrapper(mlir::ModuleOp module, sim::SimFuncOp body,
                          sim::SimFuncOp actor, llvm::StringRef wrapperName,
                          uint32_t actorSlot, uint32_t continuation,
                          const SimulationProcessFrameAnalysis &analysis,
                          bool materializeBody = true,
                          DirectFragmentTerminationCache *cache = nullptr);
mlir::LogicalResult
makeDirectFragmentBody(mlir::LLVM::LLVMFuncOp wrapper, sim::SimFuncOp body,
                       sim::SimFuncOp actor, uint32_t actorSlot,
                       uint32_t continuation,
                       const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult makeRuntimeCheckpointWrapper(mlir::ModuleOp module,
                                                 sim::SimFuncOp actor,
                                                 llvm::StringRef wrapperName,
                                                 uint32_t actorSlot,
                                                 uint32_t continuation);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONPROCESSWRAPPERLOWERING_H
