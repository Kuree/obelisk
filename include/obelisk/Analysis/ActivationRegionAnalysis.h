//===- ActivationRegionAnalysis.h - Activation regions ---------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONREGIONANALYSIS_H
#define OBELISK_ANALYSIS_ACTIVATIONREGIONANALYSIS_H

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace obelisk::analysis {

/// A revision-local structured view of one change-sensitive activation. The
/// original IR is never changed. Standard CFG-to-SCF conversion retains loops
/// and conditional regions; it does not enumerate executable iterations.
/// Initial/resumed activations must normalize to equivalent CFGs, and every
/// remaining CFG cycle must have a bounded-loop termination certificate.
///
/// Memory facts describe the source activation, including intermediate writes.
/// This is NOT permission to coalesce publications, remove watches, activate
/// an otherwise inactive process, or disregard outside observers.
class ActivationRegionAnalysis {
public:
  using Ranges = ActivationMemoryAnalysis::Ranges;
  struct Node {
    mlir::Operation *operation;
    Ranges reads, writes;
  };
  /// Optional diagnostic views; the callback must not retain their IR.
  using Trace = llvm::function_ref<void(llvm::StringRef, mlir::Operation *)>;
  ActivationRegionAnalysis(sim::SimFuncOp function,
                           const HandleDataflowAnalysis &handles,
                           Trace trace = {});
  bool isValid() const { return reason.empty(); }
  llvm::StringRef getFailureReason() const { return reason; }
  llvm::ArrayRef<Node> getNodes() const { return nodes; }
  mlir::func::FuncOp getStructuredFunction() const { return structured.get(); }
  const ActivationMemoryAnalysis &getMemory() const { return memory; }
  mlir::Operation *getSuspension() const { return suspension; }

private:
  ActivationMemoryAnalysis memory;
  mlir::OwningOpRef<mlir::func::FuncOp> structured;
  llvm::SmallVector<Node> nodes;
  mlir::Operation *suspension = nullptr;
  llvm::StringRef reason = "activation shape";
};

struct ActivationRegionStep {
  const ActivationRegionAnalysis *activation;
  mlir::Operation *operation;
};
/// Topologically order structured regions while preserving each activation's
/// source order. Cross-activation edges use canonical memory ranges. This is
/// an ordering query, not an activation or observer certificate.
/// On failure, optionally return one directed cycle, without repeating its
/// first step. The steps refer to the input analyses' structured views.
std::optional<llvm::SmallVector<ActivationRegionStep>> getActivationRegionOrder(
    llvm::ArrayRef<const ActivationRegionAnalysis *> activations,
    llvm::SmallVectorImpl<ActivationRegionStep> *cycle = nullptr);
} // namespace obelisk::analysis
#endif
