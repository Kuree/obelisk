#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOSCHEDULE_NATIVEPIPELINE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOSCHEDULE_NATIVEPIPELINE_H
#include "../SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../SimulationToLLVMCoroutine/SimulationProcessActivationLowering.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/DataLayout.h"
#include <chrono>

namespace obelisk::detail {
/// Target frame facts are captured before specialization and intentionally
/// survive the certified rewrites in this pipeline. Recomputing them from a
/// specialized body would change the native/bytecode fallback ABI. Each pass
/// explicitly preserves this module analysis; unrelated mutations invalidate
/// it, and dependent passes diagnose a missing prerequisite instead of using
/// stale operation pointers. No state is shared between modules or pass copies.
struct NativePipelineAnalysis {
  explicit NativePipelineAnalysis(mlir::Operation *operation)
      : module(mlir::cast<mlir::ModuleOp>(operation)),
        context(operation->getContext()), dataLayout("") {}
  enum class Stage {
    Empty,
    Inputs,
    State,
    Frames,
    Actors,
    Captures,
    Schedule,
    Roots,
    CleanNBA,
    EvalVariants,
    Fragments,
    Ownership,
    ExecutableNodes,
    Resolved,
    Materialized
  };
  Stage stage = Stage::Empty;
  mlir::ModuleOp module;
  mlir::MLIRContext *context;
  llvm::DataLayout dataLayout;
  bool detailedTiming = false;
  std::chrono::steady_clock::time_point lastTiming;
  void markTiming(llvm::StringRef name);
  mlir::LogicalResult initialize();
  mlir::LogicalResult planState();
  mlir::LogicalResult prepareFrames();
  mlir::LogicalResult planActors();
  mlir::LogicalResult specializeCaptures();
  mlir::LogicalResult planSchedule();
  mlir::LogicalResult prepareRoots();
  mlir::LogicalResult markCleanNBA();
  mlir::LogicalResult specializeEval();
  mlir::LogicalResult prepareFragments();
  mlir::LogicalResult planOwnership();
  mlir::LogicalResult planExecutableNodes();
  mlir::LogicalResult resolveEval();
  mlir::LogicalResult materialize();
  std::optional<uint32_t> aotActorSlotFor(sim::SimFuncOp actor) const;
  uint32_t fusionGroupFor(uint32_t slot, uint32_t continuation) const;

  mlir::FailureOr<NativeStateLayout> stateLayout = mlir::failure();
  schedule::StaticSpecializationAttr staticSpecialization;
  schedule::StaticSuperstepAttr staticSuperstep;
  llvm::SmallVector<schedule::ComputeNBACommitAttr> staticNBACommits;
  sim::SimDesignOp metadataDesign;
  bool bytecodeOnly = false;
  analysis::SimulationVPIAnalysis vpi;
  bool hasLanguageObserver = false;
  bool hasLanguageOverride = false;
  bool hasDynamicLanguageOverride = false;
  schedule::NativeSchedulerMode nativeScheduler =
      schedule::NativeSchedulerMode::Auto;
  analysis::NativeAOTAnalysis aotEligibility;
  bool useAOT = false, evalScheduler = false;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<uint32_t>>
      aotBytecodeContinuations;
  llvm::DenseSet<std::pair<uint64_t, uint32_t>> runtimeCheckpointContinuations;
  llvm::DenseSet<uint64_t> checkpointOnlyActors;
  llvm::MapVector<mlir::Operation *,
                  std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  llvm::DenseMap<uint64_t, uint32_t> aotActorSlotsByCodeUnit;
  bool cleanSuperstep = false, staticEvalIsland = false,
       closedStaticIsland = false;
  bool staticControl = false, staticFanout = false,
       staticFanoutMetadata = false;
  bool directStaticState = false, staticNBA = false;
  bool materializeNBAAccumulators = false;
  NativeStaticNBAPlan staticNBAPlan;
  NativeStaticFanoutPlan staticFanoutPlan;
  llvm::SmallVector<NativePeriodicClock> periodicClocks;
  llvm::SmallVector<NativePeriodicAlias> periodicAliases;
  NativeThreeTierPlan threeTierPlan;
  llvm::SmallVector<obelisk_rt_static_actor_root> staticActorRoots;
  mlir::FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
      mlir::failure();
  bool guardedAOTSpecialization = false, cleanWritableEval = false;
  llvm::DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> preLowerFusionOwners;
  llvm::DenseMap<uint64_t, uint32_t> preLowerFusionSourceCodeUnits;
  llvm::DenseSet<uint64_t> preLowerGeneratedRegionCodeUnits;
  mlir::FailureOr<llvm::SmallVector<NativeDirectFragment>> directFragments =
      mlir::failure();
  llvm::DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> aotFusionGroups;
  NativeEvalOwnershipPlan evalOwnership;
  llvm::SmallVector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>
      rankedAOTNodes;
  llvm::SmallVector<obelisk_rt_native_schedule_node> executableNodes;
  llvm::MapVector<mlir::Operation *, NativeSchedulePlan> processSchedules;
  bool rootSlotZero = false;
  mlir::FailureOr<ResolvedNativeEvalPlan> resolvedEval = mlir::failure();
};
} // namespace obelisk::detail
#endif
