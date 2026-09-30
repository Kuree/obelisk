//===- SimulationAOTPlanning.h - Native AOT plan support -------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H

#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "SimulationNBALowering.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <tuple>

namespace mlir::LLVM {
class LLVMFuncOp;
}

namespace llvm {
class DataLayout;
}

namespace obelisk::detail {

struct NativeStateLayout;

struct NativeStaticFanoutPlan {
  llvm::SmallVector<obelisk_rt_static_fanout_entry> entries;
  llvm::DenseMap<std::pair<uint32_t, uint32_t>, llvm::SmallVector<uint32_t>>
      fragments;
  llvm::DenseSet<uint32_t> runtimeTransitionStates;
  bool exact = false;
};

struct NativePromotionRange {
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
};

inline constexpr llvm::StringLiteral kernelPromotionReadyName =
    "__obelisk_eval_kernel_promotion_ready_v1";

/// The one model-wide eval ready set. Every clock kernel publishes its owners
/// into this bitset, so a fragment reached through several kernels still
/// occupies a single ready bit and executes once per step.
inline constexpr llvm::StringLiteral evalModelIngressName =
    "__obelisk_aot_model_ingress_v1";

mlir::LogicalResult materializeNativeKernelPromotionReadiness(
    mlir::ModuleOp module, uint64_t stateBits,
    mlir::ArrayRef<llvm::SmallVector<NativePromotionRange>> ranges,
    mlir::ArrayRef<std::string> twoStateExecutors,
    mlir::ArrayRef<obelisk_rt_native_merged_fragment> fragments);

/// A private AOT-only implementation of one stable actor continuation.  The
/// wrapper executes the activation body without resuming the coroutine; the
/// original actor and continuation remain the fallback identity.
struct NativeDirectFragment {
  uint32_t actorSlot;
  uint32_t continuation;
  std::string body;
  std::string wrapper;
  std::string twoStateWrapper;
  std::string twoStateBody;
  /// Stable physical source continuations merged into this executor. These
  /// typed identities are resolved to current-graph fragment IDs before
  /// ownership planning; the raw source graph ordinals are never retained.
  llvm::SmallVector<std::pair<uint32_t, uint32_t>, 0> sourceOwners;
  /// Stable source code-unit identities retained when body fusion erased the
  /// original coroutine actor. These connect an outlined module executor to
  /// the current fused actor without relying on continuation or graph IDs.
  llvm::SmallVector<uint64_t, 0> sourceCodeUnits;
  /// Complete physical compute-graph coverage represented by this body.  The
  /// IDs all come from the current graph generation; pre-fusion ordinals must
  /// never be mixed into this set.
  llvm::SmallVector<uint32_t, 0> fragmentIDs;
  llvm::SmallVector<NativePromotionRange, 0> promotionRanges;
  uint32_t fusionGroup = UINT32_MAX;
  bool instanceCoordinator = false;
  bool initialActivation = false;
  bool tier2Convergence = false;
};

enum class NativeEvalFanoutOwnerKind : uint8_t {
  Runtime,
  Direct,
  PeriodicAlias,
};

/// Exact ownership of each entry in a NativeStaticFanoutPlan.  This is built
/// once while the source compute graph and fusion certificates are still
/// available.  LLVM materialization must consume this mapping rather than
/// attempting to recover owner identity from transformed continuation IDs.
struct NativeEvalFanoutOwner {
  NativeEvalFanoutOwnerKind kind = NativeEvalFanoutOwnerKind::Runtime;
  uint32_t directFragment = UINT32_MAX;
};

struct NativeEvalOwnershipPlan {
  /// Owner used by ordinary state transitions and external disturbance.
  llvm::SmallVector<NativeEvalFanoutOwner> fanoutOwners;
  /// Owner used only when a periodic clock/alias directly activates the
  /// corresponding fanout entry.  This remains separate because the static
  /// fanout table does not encode the origin of a state transition.
  llvm::SmallVector<NativeEvalFanoutOwner> periodicFanoutOwners;
};

struct NativeEvalClockKernel {
  uint32_t staticState = 0;
  uint32_t edge = 0;
  uint64_t lowBit = 0;
  uint64_t bitWidth = 0;
  std::string activeName;

  auto key() const { return std::tuple{staticState, lowBit, bitWidth, edge}; }
};

/// A pure, zero-time activation and its possible immediate dependencies.
/// Selector-dependent edges remain present. Backward publications survive a
/// ranked sweep and request another group activation; a potential SCC does
/// not by itself downgrade all of its computations.
struct NativeRankedEvalNode {
  uint32_t owner = 0;
  std::string body;
  std::string twoStateBody;
  // This is a computation-order graph, not a replacement sensitivity list.
  // IEEE 1800-2023 9.2.2.2.1 still requires static-prefix sensitivities even
  // when a configuration proof later cuts an inactive value dependency.
  llvm::SmallVector<uint32_t> successors;
  uint32_t island = 0;
};

/// Immutable result of eval scheduling analysis.  All identities are resolved
/// before LLVM CFG construction starts; emission must not infer ownership from
/// transformed symbols or recompute graph closure.
struct ResolvedNativeEvalPlan {
  llvm::SmallVector<obelisk_rt_static_fanout_entry> fanoutEntries;
  llvm::SmallVector<NativeEvalClockKernel> clockKernels;
  llvm::SmallVector<obelisk_rt_native_merged_fragment> mergedFragments;
  llvm::SmallVector<std::string> mergedExecutors;
  llvm::SmallVector<std::string> mergedTwoStateExecutors;
  llvm::SmallVector<llvm::SmallVector<NativePromotionRange>>
      mergedPromotionRanges;
  /// Periodic-only owner bit for each fanout entry, or UINT32_MAX when the
  /// generic owner is also the periodic owner.
  llvm::SmallVector<uint32_t> periodicOwnerBits;
  /// For each merged owner, exact Tier-2 owner bits consumed by execution of
  /// that complete Tier-1 coordinator.
  llvm::SmallVector<llvm::APInt> ownerSubsumptionMasks;
  llvm::SmallVector<NativeRankedEvalNode> rankedNodes;
  llvm::SmallVector<unsigned> periodicClosureRecords;
  llvm::SmallVector<unsigned> periodicEntryRecords;
  uint32_t nbaTaintWordCount = 0;
  llvm::SmallVector<llvm::SmallVector<uint64_t>> recordNBATaintMasks;
  llvm::BitVector nbaTaintedRecords;
};

/// Immutable inputs shared by the generated coordinator variants.  Keeping
/// this separate from LLVM emission prevents each variant from rediscovering
/// ownership, promotion, or NBA-taint facts from symbol names.
struct NativeEvalCoordinatorPlan {
  mlir::ArrayRef<NativeEvalClockKernel> clockKernels;
  mlir::ArrayRef<obelisk_rt_native_merged_fragment> fragments;
  mlir::ArrayRef<std::string> fourStateExecutors;
  mlir::ArrayRef<std::string> twoStateExecutors;
  mlir::ArrayRef<llvm::APInt> ownerSubsumptionMasks;
  mlir::ArrayRef<NativeRankedEvalNode> rankedNodes;
  mlir::ArrayRef<llvm::SmallVector<uint64_t>> nbaTaintMasks;
  const llvm::BitVector &nbaTaintedOwners;
  uint32_t nbaTaintWordCount = 0;
  bool prioritySignalHandoff = false;
  /// Dynamic slots are staged independently of the fixed-root dirty bitmap.
  mlir::ArrayRef<std::string> dynamicNBAValidNames;
  bool hasOrderedNBA = false;
  /// Shared bounded ranked sweeps, indexed by the original owner identity.
  mlir::ArrayRef<std::string> rankedGroupExecutors;
};

struct NativeThreeTierKernelPlan {
  uint32_t id = 0;
  uint32_t owner = 0;
  uint32_t readyBit = 0;
  schedule::SchedulerTierKind tier = schedule::SchedulerTierKind::Tier3;
  schedule::ComputeScheduleKind schedule =
      schedule::ComputeScheduleKind::Acyclic;
  bool loweringReady = false;
  uint32_t memberCount = 0;
  llvm::SmallVector<uint32_t> memberIDs;
  bool twoStateEligible = false;
  llvm::SmallVector<NativePromotionRange> promotionRanges;
};

struct NativeThreeTierIngressPlan {
  uint32_t fragment = 0;
  uint32_t owner = 0;
  uint32_t readyBit = 0;
};

struct NativeThreeTierPlan {
  uint32_t ownerCount = 0;
  schedule::ComputeGraphAttr sourceGraph;
  llvm::SmallVector<NativeThreeTierKernelPlan> kernels;
  llvm::SmallVector<NativeThreeTierIngressPlan> ingress;
};

/// A structurally proven free-running clock.  The plan records physical state
/// identity rather than a source-level name, so aliases are detected and
/// multiple clocks can be ordered by their calendar deadlines.
using NativePeriodicClock = schedule::PeriodicClockAttr;

/// A proven one-bit, single-driver port projection of a periodic source.  The
/// generated loop updates both canonical driver and resolved-net planes and
/// seeds the target fanout directly, avoiding a forwarding actor per edge.
using NativePeriodicAlias = schedule::PeriodicAliasAttr;

llvm::DenseMap<analysis::ClockBit, analysis::ClockFact> buildNativeClockInferencePlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const mlir::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    llvm::ArrayRef<NativePeriodicClock> clocks);

mlir::LogicalResult
specializeNativeAOTCaptures(mlir::ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility);
mlir::FailureOr<llvm::SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    const llvm::DenseSet<uint64_t> &checkpointOnlyActors);
mlir::FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    const llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Block *>>
        &bytecodeFragments,
    const llvm::DenseSet<mlir::Operation *> &runtimeOwnedFanoutActors,
    bool enabled, bool certifiedStaticIsland);
mlir::FailureOr<NativeThreeTierPlan>
buildNativeThreeTierPlan(mlir::ModuleOp module,
                         const NativeStateLayout &stateLayout);
mlir::FailureOr<NativeEvalOwnershipPlan> buildNativeEvalOwnershipPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const NativeStaticFanoutPlan &fanoutPlan,
    mlir::ArrayRef<NativeDirectFragment> directFragments,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases);
mlir::FailureOr<ResolvedNativeEvalPlan> resolveNativeEvalPlan(
    mlir::ModuleOp module,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<NativeDirectFragment> directFragments,
    const NativeEvalOwnershipPlan &evalOwnership,
    schedule::ComputeGraphAttr computeGraph,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases);
mlir::LogicalResult materializeNativeEvalGroupBodies(mlir::ModuleOp module);

mlir::FailureOr<llvm::SmallVector<std::string>> materializeNativeRankedGroups(
    mlir::ModuleOp module, const NativeEvalCoordinatorPlan &plan);

mlir::LogicalResult materializeNativeEvalDispatch(
    mlir::ModuleOp module, const NativeEvalCoordinatorPlan &plan);
mlir::FailureOr<llvm::SmallVector<NativePeriodicClock>>
buildNativePeriodicClockPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots);
mlir::FailureOr<llvm::SmallVector<NativePeriodicAlias>>
buildNativePeriodicAliasPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks);
mlir::LogicalResult materializeNativePeriodicClockPlan(
    mlir::ModuleOp module, mlir::ArrayRef<NativePeriodicClock> periodicClocks);
void emitScalarNBACommitLoop(
    mlir::OpBuilder &builder, mlir::ModuleOp module,
    mlir::LLVM::LLVMFuncOp function, mlir::Block *exit, mlir::Value region,
    const NativeStaticNBAPlan &plan,
    mlir::ArrayRef<llvm::SmallVector<uint32_t>> rootsByWord,
    mlir::ArrayRef<obelisk_rt_static_fanout_entry> fanout,
    mlir::Value activatedNodes, mlir::Value activatedDirect,
    unsigned directWords);

mlir::LogicalResult makeNativeAOTPlanLegacy(
    mlir::ModuleOp module, const llvm::DataLayout &dataLayout,
    uint32_t actorCount,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    bool enableDirectState, bool enableStaticNBA, bool enableStaticControl,
    bool enableStaticFanout, bool enableCleanSuperstep, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);
mlir::FailureOr<bool> makeNativeEvalPlan(
    mlir::ModuleOp module, const llvm::DataLayout &dataLayout,
    uint32_t actorCount,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const ResolvedNativeEvalPlan &resolvedPlan,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    mlir::ArrayRef<NativeDirectFragment> directFragments,
    const NativeEvalOwnershipPlan &evalOwnership,
    schedule::ComputeGraphAttr computeGraph,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool fullyStatic, bool staticEvalIsland,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
