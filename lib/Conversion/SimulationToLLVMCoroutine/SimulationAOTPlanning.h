//===- SimulationAOTPlanning.h - Native AOT plan support -------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H

#include "SimulationNBALowering.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

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
  /// Stable pre-fusion actor/continuation identities exclusively represented
  /// by this generated body.
  llvm::SmallVector<std::pair<uint32_t, uint32_t>, 0> sourceOwners;
  llvm::SmallVector<uint32_t, 0> fragmentIDs;
  // Pre-fusion suspension fragments exclusively represented by this body.
  // These stable physical anchors map source fanout to a fused executor
  // without relying on actor names or transformed continuation ordinals.
  llvm::SmallVector<uint32_t, 0> ownershipAnchors;
  llvm::SmallVector<NativePromotionRange, 0> promotionRanges;
  uint32_t fusionGroup = UINT32_MAX;
  bool initialActivation = false;
  bool tier2Convergence = false;
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

struct NativeThreeTierPlan {
  uint32_t ownerCount = 0;
  sim::ComputeGraphAttr sourceGraph;
  llvm::SmallVector<NativeThreeTierKernelPlan> kernels;
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
mlir::LogicalResult
materializeNativeThreeTierPlan(mlir::ModuleOp module,
                               const NativeThreeTierPlan &plan);
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
    sim::ComputeGraphAttr computeGraph,
    mlir::ArrayRef<NativePeriodicClock> periodicClocks,
    mlir::ArrayRef<NativePeriodicAlias> periodicAliases,
    mlir::ArrayRef<NativePromotionRange> evalPromotionRanges,
    bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool evalScheduler, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_AOT_PLANNING_H
