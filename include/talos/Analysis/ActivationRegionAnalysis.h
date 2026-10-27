//===- ActivationRegionAnalysis.h - Activation regions ---------*- C++ -*-===//
#ifndef TALOS_ANALYSIS_ACTIVATIONREGIONANALYSIS_H
#define TALOS_ANALYSIS_ACTIVATIONREGIONANALYSIS_H

#include "mlir/IR/OwningOpRef.h"
#include "talos/Analysis/ActivationMemoryAnalysis.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace talos::analysis {

/// A revision-local CFG view of one change-sensitive activation. The original
/// IR is never changed. The view keeps the activation's canonical CFG: loops
/// and conditional regions stay as blocks, and no executable iteration is
/// enumerated. Initial/resumed activations must normalize to equivalent CFGs,
/// and every remaining CFG cycle must have a bounded-loop termination
/// certificate.
///
/// Scheduling units are the activation's top-level statements. A chain block
/// executes exactly once on every path from entry to the suspension: it is a
/// dominator of the exit and belongs to no CFG cycle. Each operation of a
/// chain block is one unit. The branch from one chain block to the next,
/// together with every block strictly between them, is one region unit; its
/// results are the next chain block's arguments.
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
    UnmappedMemoryAccess,
  };
  enum class TracePhase { Initial, Resume };
  /// Clients materializing the CFG view may prepare dispatch edges.
  /// Summary-only clients can retain the existing canonical CFG policy.
  enum class ControlFlowPreparation { Canonical, ThreadDispatch };
  using Ranges = ActivationMemoryAnalysis::Ranges;
  struct Node {
    /// An operation unit, or the branch that enters a region unit.
    mlir::Operation *operation;
    /// Region units only: the blocks strictly inside the region, in CFG order,
    /// and the chain block entered when the region exits.
    llvm::SmallVector<mlir::Block *> blocks;
    mlir::Block *join = nullptr;
    Ranges reads, writes;
    bool isRegion() const { return join != nullptr; }
    /// Visit every operation of the unit, including a region's entry branch.
    void walk(llvm::function_ref<void(mlir::Operation *)> callback) const;
  };
  /// Optional diagnostic views; the callback must not retain their IR.
  using Trace = llvm::function_ref<void(TracePhase, mlir::Operation *)>;
  ActivationRegionAnalysis(
      sim::SimFuncOp function, const HandleDataflowAnalysis &handles,
      Trace trace = {},
      ActivationMemoryAnalysis::DependencyMode mode =
          ActivationMemoryAnalysis::DependencyMode::Accesses,
      ControlFlowPreparation control = ControlFlowPreparation::Canonical);
  bool isValid() const { return reason == FailureReason::None; }
  FailureReason getFailureReason() const { return reason; }
  llvm::ArrayRef<Node> getNodes() const { return nodes; }
  /// The unit containing an operation of the CFG view, or nullptr.
  const Node *getNode(mlir::Operation *operation) const;
  /// The unit defining a value of the CFG view, or nullptr for an entry
  /// argument. A region unit defines its join block's arguments.
  const Node *getDefiningNode(mlir::Value value) const;
  /// The initial activation as a closed function: its suspension is replaced
  /// by a return. Memory facts refer to this view's operations.
  sim::SimFuncOp getActivationFunction() const { return view.get(); }
  const ActivationMemoryAnalysis &getMemory() const { return memory; }
  mlir::Operation *getSuspension() const { return suspension; }

private:
  ActivationMemoryAnalysis memory;
  mlir::OwningOpRef<sim::SimFuncOp> view;
  llvm::SmallVector<Node> nodes;
  llvm::DenseMap<mlir::Operation *, unsigned> nodeIndices;
  llvm::DenseMap<mlir::Block *, unsigned> argumentNodes;
  mlir::Operation *suspension = nullptr;
  FailureReason reason = FailureReason::ActivationShape;
};

llvm::StringRef
stringifyActivationFailure(ActivationRegionAnalysis::FailureReason reason);

struct ActivationRegionStep {
  const ActivationRegionAnalysis *activation;
  /// The unit's Node::operation.
  mlir::Operation *operation;
};

/// Data inputs of an individual publication, following SSA values, CFG block
/// arguments and the branches that control the store. Unlike a scheduling
/// node, this does not union unrelated computations in the same loop. In
/// ReachingValues mode, reads are incoming value dependencies of each output
/// lane, following definite local stores. Accesses mode conservatively uses
/// the physical read ranges of SSA load inputs.
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
/// Topologically order activation units while preserving each original
/// process's source order, SSA dependencies and canonical memory dependencies.
/// Complete typed source-owner provenance permits independent original owners
/// in an existing kernel to interleave. Otherwise retain the whole activation's
/// source order. This is not an activation or observer certificate.
/// On failure, optionally return one directed cycle, without repeating its
/// first step. The steps refer to the input analyses' CFG views.
std::optional<llvm::SmallVector<ActivationRegionStep>> getActivationRegionOrder(
    llvm::ArrayRef<const ActivationRegionAnalysis *> activations,
    llvm::SmallVectorImpl<ActivationRegionStep> *cycle = nullptr);

/// Stores of one activation that execute together, as one clone of that
/// activation's CFG in which every other store is removed.
struct ActivationSlice {
  const ActivationRegionAnalysis *activation;
  llvm::SmallVector<mlir::Operation *> stores;
};
/// Clone a slice and simplify the now-unneeded control flow. Private stores
/// unrelated to its publications are removed too. The mapping, when supplied,
/// describes the clone before simplification; surviving memory reads retain
/// their operation identity. The original activation is unchanged.
mlir::OwningOpRef<sim::SimFuncOp>
cloneActivationSlice(const ActivationSlice &slice,
                     mlir::IRMapping *mapping = nullptr);
/// Order the stores of all activations by data dependence alone, ignoring each
/// process's statement order, and group consecutive stores of one activation
/// into slices. A store depends on the loads that its value, its address and
/// its execution depend on (SSA values, block arguments and controlling
/// branches). Overlapping stores and source-local store/load dependences share
/// a slice, preserving snapshots and source order. Loops are never unrolled:
/// a slice keeps the complete
/// CFG.
///
/// This is not an observer certificate. Reordering statements of a process is
/// not a reference-algorithm execution (IEEE 1800-2023 4.6(a)); it is valid
/// only under 4.3 when nothing outside the cohort can observe the transitions
/// it removes. On failure, optionally return one directed cycle of stores.
std::optional<llvm::SmallVector<ActivationSlice>> getActivationSliceOrder(
    llvm::ArrayRef<const ActivationRegionAnalysis *> activations,
    llvm::SmallVectorImpl<ActivationRegionStep> *cycle = nullptr);
} // namespace talos::analysis
#endif
