//===- SimulationAOTPlanning.h - Native AOT plan support -------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H

#include "SimulationNBALowering.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <tuple>

namespace obelisk::detail {

struct NativeStateLayout;

struct NativeStaticFanoutPlan {
  llvm::SmallVector<obelisk_rt_static_fanout_entry> entries;
  llvm::DenseMap<std::pair<uint32_t, uint32_t>, llvm::SmallVector<uint32_t>>
      fragments;
  bool exact = false;
};

struct NativePromotionRange {
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
};

/// A private AOT-only implementation of one stable actor continuation.  The
/// wrapper executes the activation body without resuming the coroutine; the
/// original actor and continuation remain the fallback identity.
struct NativeDirectFragment {
  uint32_t actorSlot;
  uint32_t continuation;
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
  llvm::SmallVector<NativeEvalFanoutOwner> fanoutOwners;
};

struct NativeEvalClockKernel {
  uint32_t staticState = 0;
  uint32_t edge = 0;
  uint64_t lowBit = 0;
  uint64_t bitWidth = 0;
  std::string ingressName;
  std::string activeName;

  auto key() const { return std::tuple{staticState, lowBit, bitWidth, edge}; }
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
  mlir::ArrayRef<std::string> promotionReadyFunctions;
  mlir::ArrayRef<llvm::SmallVector<uint64_t>> nbaTaintMasks;
  const llvm::BitVector &nbaTaintedOwners;
  uint32_t nbaTaintWordCount = 0;
};

struct NativeEvalCoordinatorOptions {
  bool promoted = false;
  bool hybrid = false;
  uint64_t allowedOwnerMask = UINT64_MAX;
  /// Pending bits that require a return to the hybrid coordinator. Owners
  /// with an intrinsic path dispatcher may remain pending for the stronger
  /// whole-closure certificate without blocking owner-local steady routing.
  uint64_t pendingGuardMask = UINT64_MAX;
  bool trustedTwoState = false;
  bool guardPendingOwners = false;
  bool observePathFallback = false;
};

struct NativeThreeTierKernelPlan {
  uint32_t id = 0;
  uint32_t owner = 0;
  uint32_t readyBit = 0;
  sim::SchedulerTierKind tier = sim::SchedulerTierKind::Tier3;
  sim::ComputeScheduleKind schedule = sim::ComputeScheduleKind::Acyclic;
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
  sim::ComputeGraphAttr sourceGraph;
  llvm::SmallVector<NativeThreeTierKernelPlan> kernels;
  llvm::SmallVector<NativeThreeTierIngressPlan> ingress;
};

/// A structurally proven free-running clock.  The plan records physical state
/// identity rather than a source-level name, so aliases are detected and
/// multiple clocks can be ordered by their calendar deadlines.
struct NativePeriodicClock {
  uint32_t actorSlot = 0;
  uint32_t continuation = 0;
  uint32_t staticState = 0;
  uint64_t bitOffset = 0;
  uint64_t halfPeriod = 0;
};

/// A proven one-bit, single-driver port projection of a periodic source.  The
/// generated loop updates both canonical driver and resolved-net planes and
/// seeds the target fanout directly, avoiding a forwarding actor per edge.
struct NativePeriodicAlias {
  uint32_t sourceStaticState = 0;
  uint32_t forwardingActorSlot = 0;
  uint32_t forwardingContinuation = 0;
  uint32_t targetStaticState = 0;
  uint64_t sourceBitOffset = 0;
  uint64_t targetBitOffset = 0;
  uint64_t driverBitOffset = 0;
};

mlir::LogicalResult
specializeNativeAOTCaptures(mlir::ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility);
mlir::FailureOr<llvm::SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots);
mlir::FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    mlir::ModuleOp module, const NativeStateLayout &stateLayout,
    const llvm::DenseMap<mlir::Operation *, uint32_t> &actorSlots,
    bool enabled);
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
    sim::ComputeGraphAttr computeGraph,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases);
mlir::LogicalResult materializeNativeEvalCoordinator(
    mlir::ModuleOp module, const NativeEvalCoordinatorPlan &plan,
    mlir::StringRef functionName, mlir::ArrayRef<std::string> executors,
    NativeEvalCoordinatorOptions options);
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
mlir::LogicalResult makeNativeAOTPlanLegacy(
    mlir::ModuleOp module, uint32_t actorCount,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    bool enableDirectState, bool enableStaticNBA, bool enableStaticControl,
    bool enableStaticFanout, bool enableCleanSuperstep, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);
mlir::LogicalResult makeNativeEvalPlan(
    mlir::ModuleOp module, uint32_t actorCount,
    mlir::ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    mlir::ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    mlir::ArrayRef<NativeDirectFragment> directFragments,
    const NativeEvalOwnershipPlan &evalOwnership,
    sim::ComputeGraphAttr computeGraph,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases,
    bool enableDirectState, bool enableStaticNBA, bool enableStaticControl,
    bool enableStaticFanout, bool enableCleanSuperstep, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
