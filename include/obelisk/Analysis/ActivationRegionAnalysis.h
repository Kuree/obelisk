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
  enum class FailureReason {
    None,
    ActivationShape,
    InitialResumeEquivalence,
    Termination,
    MemoryEffects,
    CFGStructuring,
    UnsupportedRegionSSASlot,
    UnmappedStructuredMemoryAccess,
  };
  enum class TracePhase { Initial, Resume };
  using Ranges = ActivationMemoryAnalysis::Ranges;
  struct Node {
    mlir::Operation *operation;
    Ranges reads, writes;
  };
  /// Optional diagnostic views; the callback must not retain their IR.
  using Trace = llvm::function_ref<void(TracePhase, mlir::Operation *)>;
  ActivationRegionAnalysis(
      sim::SimFuncOp function, const HandleDataflowAnalysis &handles,
      Trace trace = {},
      ActivationMemoryAnalysis::DependencyMode mode =
          ActivationMemoryAnalysis::DependencyMode::Accesses);
  bool isValid() const { return reason == FailureReason::None; }
  FailureReason getFailureReason() const { return reason; }
  llvm::ArrayRef<Node> getNodes() const { return nodes; }
  mlir::func::FuncOp getStructuredFunction() const { return structured.get(); }
  const ActivationMemoryAnalysis &getMemory() const { return memory; }
  mlir::Operation *getSuspension() const { return suspension; }

private:
  ActivationMemoryAnalysis memory;
  mlir::OwningOpRef<mlir::func::FuncOp> structured;
  llvm::SmallVector<Node> nodes;
  mlir::Operation *suspension = nullptr;
  FailureReason reason = FailureReason::ActivationShape;
};

llvm::StringRef
stringifyActivationFailure(ActivationRegionAnalysis::FailureReason reason);

struct ActivationRegionStep {
  const ActivationRegionAnalysis *activation;
  mlir::Operation *operation;
};

/// Data inputs of an individual publication, following SSA result/yield lanes.
/// Unlike a scheduling node, this does not union unrelated computations in the
/// same structured loop. In ReachingValues mode, reads are incoming value
/// dependencies of each output lane, following definite local stores. Accesses
/// mode conservatively uses the physical read ranges of SSA load inputs.
/// Publication/observer ordering remains a separate scheduling obligation.
struct ActivationPublication {
  mlir::Operation *operation;
  ActivationMemoryAnalysis::Ranges reads, writes;
};
std::optional<llvm::SmallVector<ActivationPublication>>
getActivationPublications(const ActivationRegionAnalysis &activation);

/// Query the publication value graph, independently of source/effect ordering.
/// A DAG here is necessary information for further scheduling refinement, not
/// permission to split or reorder the source activation.
std::optional<llvm::SmallVector<ActivationRegionStep>>
getActivationPublicationOrder(
    llvm::ArrayRef<const ActivationRegionAnalysis *> activations,
    llvm::SmallVectorImpl<ActivationRegionStep> *cycle = nullptr);
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
