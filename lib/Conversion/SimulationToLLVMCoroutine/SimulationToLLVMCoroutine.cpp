//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"

#include "SimulationAOTPlanning.h"
#include "SimulationNBALowering.h"
#include "SimulationPackedLowering.h"
#include "SimulationProcessActivationLowering.h"
#include "SimulationProcessCoroutineLowering.h"
#include "SimulationProcessFunctionLowering.h"
#include "SimulationProcessWrapperLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Conversion/FunctionalCoverageSchemaVerification.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/OutputItemFlags.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MathToLLVM/MathToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/Math/Transforms/Passes.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "mlir/Transforms/RegionUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <chrono>
#include <limits>

using namespace mlir;

namespace obelisk {

static void populateSimulationCoroutineBodyToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns,
    SymbolTableCollection *symbolTables = nullptr);

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ExpandIntegerPower final : public OpRewritePattern<math::IPowIOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(math::IPowIOp op,
                                PatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto type = cast<IntegerType>(op.getType());
    Value one = arith::ConstantOp::create(rewriter, loc, type,
                                          rewriter.getIntegerAttr(type, 1));
    SmallVector<Type> loopTypes{type, type, type};
    SmallVector<Value> initial{one, op.getLhs(), op.getRhs()};
    auto loop = scf::WhileOp::create(
        rewriter, loc, loopTypes, initial,
        [&](OpBuilder &nested, Location nestedLoc, ValueRange arguments) {
          Value zero = arith::ConstantOp::create(
              nested, nestedLoc, type, nested.getIntegerAttr(type, 0));
          Value keepGoing = arith::CmpIOp::create(
              nested, nestedLoc, arith::CmpIPredicate::ne, arguments[2], zero);
          scf::ConditionOp::create(nested, nestedLoc, keepGoing, arguments);
        },
        [&](OpBuilder &nested, Location nestedLoc, ValueRange arguments) {
          Value lowBit = arguments[2];
          if (type.getWidth() != 1)
            lowBit = arith::TruncIOp::create(nested, nestedLoc,
                                             nested.getI1Type(), lowBit);
          Value multiplied = arith::MulIOp::create(nested, nestedLoc,
                                                   arguments[0], arguments[1]);
          Value selected = arith::SelectOp::create(nested, nestedLoc, lowBit,
                                                   multiplied, arguments[0]);
          Value squared = arith::MulIOp::create(nested, nestedLoc, arguments[1],
                                                arguments[1]);
          Value one = arith::ConstantOp::create(nested, nestedLoc, type,
                                                nested.getIntegerAttr(type, 1));
          Value remaining =
              arith::ShRUIOp::create(nested, nestedLoc, arguments[2], one);
          scf::YieldOp::create(nested, nestedLoc,
                               ValueRange{selected, squared, remaining});
        });
    if (!loop || loop->getNumResults() != loopTypes.size())
      return rewriter.notifyMatchFailure(op,
                                         "failed to build power loop results");
    SmallVector<Value> replacement{loop.getResult(0)};
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

using detail::annotateCompactNBAMetadata;
using detail::buildNativeEvalOwnershipPlan;
using detail::buildNativePeriodicAliasPlan;
using detail::buildNativePeriodicClockPlan;
using detail::buildNativeStateLayout;
using detail::buildNativeStaticActorRootPlan;
using detail::buildNativeStaticFanoutPlan;
using detail::buildNativeStaticNBAPlan;
using detail::buildNativeThreeTierPlan;
using detail::convertProcessType;
using detail::declareNativeRuntimeABI;
using detail::evalRuntimeNBAFallbackAttr;
using detail::evalRuntimeNBARequiredAttr;
using detail::finishPreparedPlainNativeProcess;
using detail::finishPreparedSuspendableProcess;
using detail::insertAutomaticOwnerReleases;
using detail::instrumentManagedRoots;
using detail::lowerPackedSimulationOperations;
using detail::lowerPreparedOrdinaryFunction;
using detail::lowerPreparedPlainNativeProcess;
using detail::lowerPreparedSuspendableProcess;
using detail::makeDirectFragmentWrapper;
using detail::makeNativeAOTPlanLegacy;
using detail::makeNativeEvalPlan;
using detail::makeProcessActivationHelper;
using detail::makeProcessDescriptor;
using detail::makeProcessSpawnHelper;
using detail::makeRuntimeCheckpointWrapper;
using detail::makeSchedulerMain;
using detail::makeStatePlane;
using detail::markCleanStaticNBAsInGuardedBodies;
using detail::materializeCleanEvalBodies;
using detail::materializeDPIThunks;
using detail::materializeEvalTwoStateVariants;
using detail::materializeGeneratedNBAAccumulators;
using detail::materializeManagedMethodThunks;
using detail::materializeNativeDPIExportThunks;
using detail::materializeNativeObserverThunks;
using detail::materializeNativePeriodicClockPlan;
using detail::materializeNativeSchedulerGlobals;
using detail::NativeDirectFragment;
using detail::NativeEvalFanoutOwner;
using detail::NativeEvalFanoutOwnerKind;
using detail::NativeEvalOwnershipPlan;
using detail::NativePeriodicAlias;
using detail::NativePeriodicClock;
using detail::NativePromotionRange;
using detail::NativeSchedulePlan;
using detail::NativeStateLayout;
using detail::NativeStaticFanoutPlan;
using detail::NativeStaticNBAPlan;
using detail::NativeThreeTierKernelPlan;
using detail::NativeThreeTierPlan;
using detail::populateContextRuntimeToLLVMConversionPattern;
using detail::PreparedOrdinaryNativeFunction;
using detail::PreparedPlainNativeProcess;
using detail::PreparedSuspendableProcess;
using detail::prepareManagedLowering;
using detail::prepareOrdinaryFunction;
using detail::preparePlainNativeProcess;
using detail::prepareSuspendableProcess;
using detail::specializeNativeAOTCaptures;
using detail::stableProcessID;
using detail::threadProcessStateThroughCFG;

LogicalResult verifyGeneratedEvalCallClosures(ModuleOp module) {
  if (!::obelisk::schedule::has<::obelisk::schedule::Field::EvalGenerated>(
          module))
    return success();
  // Verification does not mutate symbols. Build the index once instead of
  // scanning the module for each edge in the generated call closure.
  SymbolTable symbols(module);
  constexpr auto allowedCalleesAttr =
      ::obelisk::schedule::Field::EvalAllowedCallees;
  // This query only reads the scheduler's priority-handoff latch.  Generated
  // coordinators use it to leave the hot closure before a Reactive
  // concurrent-disable observer runs; it cannot mutate or re-enter the
  // scheduler like the runtime calls rejected below.
  constexpr StringLiteral prioritySignalQuery =
      "obelisk_rt_v1_scheduler_priority_signal_pending";
  // Pure Table 28-8 strength combination takes only value arguments and has
  // no scheduler/context edge. Gate-region eval bodies may therefore retain
  // it just like an ordinary outlined arithmetic helper; ThinLTO can inline
  // the small resolver in production builds.
  constexpr StringLiteral strengthResolveQuery =
      "obelisk_rt_v1_strength_resolve_kind";
  // A line hit is a relaxed atomic increment through the context argument
  // already present on every generated eval body. It cannot allocate, lock,
  // re-enter the scheduler, or alter design state, so worker-lane eval bodies
  // may retain it as an explicit observable side effect.
  constexpr StringLiteral coveragePointHit = "obelisk_rt_v1_coverage_point_hit";
  SmallVector<LLVM::LLVMFuncOp> pending;
  llvm::SmallPtrSet<Operation *, 32> visited;
  for (LLVM::LLVMFuncOp function : module.getOps<LLVM::LLVMFuncOp>()) {
    if (!::obelisk::schedule::has<schedule::metadata::evalCallClosureRoot>(
            function))
      continue;
    pending.push_back(function);
  }
  while (!pending.empty()) {
    LLVM::LLVMFuncOp function = pending.pop_back_val();
    if (!visited.insert(function.getOperation()).second)
      continue;
    WalkResult result = function.walk([&](LLVM::CallOp call) {
      SmallVector<FlatSymbolRefAttr> targets;
      if (std::optional<StringRef> callee = call.getCallee()) {
        if (callee->starts_with("obelisk_rt_")) {
          if (*callee == prioritySignalQuery ||
              *callee == strengthResolveQuery || *callee == coveragePointHit ||
              // A value helper's post-call termination poll only reads the
              // context's finish latch. It cannot execute actors, publish
              // state, advance time or request termination itself.
              *callee == "obelisk_rt_v1_scheduler_termination_requested" ||
              // Generated run_until updates the canonical scheduler time
              // before dispatch. This query only reads that same timestamp.
              *callee == "obelisk_rt_v1_scheduler_time" ||
              *callee == "obelisk_rt_v1_eval_display" ||
              // Cold allocation of private generated NBA storage. This never
              // executes actors, changes design state or re-enters scheduling.
              *callee == "obelisk_rt_v1_eval_nba_reserve" ||
              // The reserve failure block latches an error and returns from
              // the activation. It cannot schedule or execute an actor.
              *callee == "obelisk_rt_v1_scheduler_fail" ||
              // Exact proof-state publication over compiler-verified tables.
              // The helper cannot allocate, execute actors, access canonical
              // state, advance time, or re-enter the scheduler.
              *callee == "obelisk_rt_v1_native_promotion_invalidate_ranges" ||
              *callee == "obelisk_rt_v1_native_promotion_recheck_ranges")
            return WalkResult::advance();
          call.emitError("generated eval hot closure calls runtime symbol ")
              << *callee << " in " << function.getSymName();
          return WalkResult::interrupt();
        }
        targets.push_back(FlatSymbolRefAttr::get(module.getContext(), *callee));
      } else if (auto allowed =
                     ::obelisk::schedule::get<allowedCalleesAttr>(call)) {
        for (Attribute attribute : allowed) {
          auto target = dyn_cast<FlatSymbolRefAttr>(attribute);
          if (!target) {
            call.emitError("generated eval indirect call has malformed "
                           "allowed-callee metadata");
            return WalkResult::interrupt();
          }
          targets.push_back(target);
        }
      } else {
        call.emitError(
            "generated eval indirect call has no closed target set in ")
            << function.getSymName();
        return WalkResult::interrupt();
      }
      for (FlatSymbolRefAttr target : targets) {
        if (target.getValue().starts_with("obelisk_rt_")) {
          call.emitError("generated eval indirect route targets runtime "
                         "symbol ")
              << target.getValue();
          return WalkResult::interrupt();
        }
        LLVM::LLVMFuncOp callee =
            symbols.lookup<LLVM::LLVMFuncOp>(target.getValue());
        if (!callee) {
          if (!target.getValue().starts_with("llvm.")) {
            call.emitError("generated eval hot closure calls unresolved "
                           "external symbol ")
                << target.getValue();
            return WalkResult::interrupt();
          }
          continue;
        }
        if (callee.isExternal() && !callee.getSymName().starts_with("llvm.")) {
          call.emitError("generated eval hot closure calls external symbol ")
              << callee.getSymName();
          return WalkResult::interrupt();
        }
        pending.push_back(callee);
      }
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();
  }
  return success();
}

FailureOr<SmallVector<NativeDirectFragment>> materializeDirectFragments(
    ModuleOp module, sim::SimDesignOp design,
    const DenseMap<uint64_t, uint32_t> &actorSlotsByCodeUnit,
    const llvm::MapVector<
        Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>> &analyses,
    const DenseMap<Operation *, SmallVector<uint32_t>> &bytecodeContinuations,
    const DenseSet<uint64_t> &generatedRegionCodeUnits,
    const DenseSet<std::pair<uint64_t, uint32_t>>
        &runtimeCheckpointContinuations,
    bool enabled) {
  SmallVector<NativeDirectFragment> result;
  struct PendingEvalWrapper {
    sim::SimFuncOp body;
    sim::SimFuncOp twoStateBody;
    sim::SimFuncOp actor;
    std::string wrapper;
    std::string twoStateWrapper;
    uint32_t actorSlot;
    uint32_t continuation;
    const SimulationProcessFrameAnalysis *analysis;
    std::optional<bool> initialActivation;
    SmallVector<uint32_t> fragmentIDs;
    bool runtimeCheckpoint = false;
  };
  SmallVector<PendingEvalWrapper> pendingEvalWrappers;
  SmallVector<Attribute> checkpointRoutes;
  if (!enabled || !design)
    return result;
  // Keep new direct bodies in this index so later name checks and closure
  // lookups see them without rescanning the design for every call.
  SymbolTable designSymbols(design);
  MLIRContext *context = module.getContext();
  auto isGeneratedRegionActor = [&](sim::SimFuncOp actor) {
    IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
    return codeUnit && generatedRegionCodeUnits.contains(codeUnit.getUInt());
  };
  auto actorSlotFor = [&](sim::SimFuncOp actor) -> std::optional<uint32_t> {
    IntegerAttr codeUnit = actor ? actor.getCodeUnitIdAttr() : IntegerAttr{};
    if (!codeUnit)
      return std::nullopt;
    auto found = actorSlotsByCodeUnit.find(codeUnit.getUInt());
    if (found == actorSlotsByCodeUnit.end())
      return std::nullopt;
    return found->second;
  };
  // Compute-graph rebuilding may renumber suspension sites after eval bodies
  // have been outlined. Resolve copied source identities through their stable
  // code unit and the final per-actor process analysis. Continuation IDs are
  // actor-scoped and must never be used as a design-wide identity.
  struct PhysicalActorIdentity {
    uint32_t slot = UINT32_MAX;
    SmallVector<uint32_t> continuations;
  };
  struct AnalyzedActorIdentity {
    Operation *operation = nullptr;
    const SimulationProcessFrameAnalysis *analysis = nullptr;
  };
  DenseMap<uint64_t, PhysicalActorIdentity> physicalActors;
  DenseMap<uint64_t, AnalyzedActorIdentity> analyzedActors;
  for (const auto &entry : analyses) {
    auto function = dyn_cast<sim::SimFuncOp>(entry.first);
    IntegerAttr codeUnit =
        function ? function.getCodeUnitIdAttr() : IntegerAttr{};
    if (!codeUnit)
      continue;
    auto [analyzedActor, analyzedInserted] = analyzedActors.try_emplace(
        codeUnit.getUInt(),
        AnalyzedActorIdentity{entry.first, entry.second.get()});
    if (!analyzedInserted && analyzedActor->second.operation != entry.first)
      return function.emitOpError("has a duplicate analyzed code-unit ID");
    std::optional<uint32_t> actorSlot = actorSlotFor(function);
    if (!actorSlot)
      continue;
    PhysicalActorIdentity identity;
    identity.slot = *actorSlot;
    for (const ProcessSuspension &suspension : entry.second->getSuspensions())
      identity.continuations.push_back(suspension.continuationID);
    llvm::sort(identity.continuations);
    identity.continuations.erase(std::unique(identity.continuations.begin(),
                                             identity.continuations.end()),
                                 identity.continuations.end());
    auto [owner, inserted] =
        physicalActors.try_emplace(codeUnit.getUInt(), std::move(identity));
    if (!inserted && owner->second.slot != *actorSlot)
      return function.emitOpError("has a duplicate physical code-unit ID");
  }
  struct ContinuationFragmentCoverage {
    SmallVector<uint32_t> members;
  };
  auto fragmentCoverageFor = [&](sim::SimFuncOp actor, uint32_t continuation) {
    ContinuationFragmentCoverage coverage;
    schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (!graph)
      return coverage;
    DenseMap<Block *, uint32_t> fragmentByBlock;
    Block *anchorBlock = nullptr;
    for (Attribute attribute : graph.getNodes()) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
      if (!fragment || fragment.getFunction().getValue() != actor.getSymName())
        continue;
      Block *block =
          analysis::lookupComputeGraphBlock(actor, fragment.getBlock());
      if (!block)
        continue;
      uint32_t fragmentID = fragment.getId();
      fragmentByBlock.try_emplace(block, fragmentID);
      schedule::ContinuationSiteAttr site;
      if (auto suspend =
              dyn_cast<sim::SimSuspendChangeOp>(block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend =
                   dyn_cast<sim::SimSuspendEdgeOp>(block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend =
                   dyn_cast<sim::SimSuspendAnyOp>(block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend =
                   dyn_cast<sim::SimSuspendObserveOp>(block->getTerminator()))
        site = suspend.getSiteAttr();
      if (site && site.getId() == continuation) {
        coverage.members.push_back(fragmentID);
        anchorBlock = block;
      }
    }
    if (anchorBlock) {
      SmallVector<Block *> pending;
      for (Block *successor : anchorBlock->getSuccessors())
        pending.push_back(successor);
      DenseSet<Block *> visited;
      visited.insert(anchorBlock);
      while (!pending.empty()) {
        Block *block = pending.pop_back_val();
        if (!visited.insert(block).second ||
            block->getParent() != &actor.getBody())
          continue;
        if (auto found = fragmentByBlock.find(block);
            found != fragmentByBlock.end())
          coverage.members.push_back(found->second);
        if (sim::isSuspensionOp(block->getTerminator()))
          continue;
        for (Block *successor : block->getSuccessors())
          pending.push_back(successor);
      }
    }
    // Ownership is meaningful only for the current compute-graph generation.
    // A body absent from that graph remains runtime-owned; FragmentABI may
    // contain ordinals from an earlier graph rebuild and must not participate
    // in exact fanout matching.
    llvm::sort(coverage.members);
    coverage.members.erase(
        std::unique(coverage.members.begin(), coverage.members.end()),
        coverage.members.end());
    return coverage;
  };
  SmallVector<sim::SimFuncOp> currentActors;
  design.walk([&](sim::SimFuncOp actor) {
    if (actorSlotFor(actor))
      currentActors.push_back(actor);
  });
  for (sim::SimFuncOp actor : currentActors) {
    // IEEE 1800-2023 4.5: this evaluator drains Active work. Reactive
    // activations must retain their runtime identity and region arbitration
    // through the entire Reactive/Re-Inactive/Re-NBA iteration, including
    // program completion (24.7), rather than joining this Active closure.
    if (actor.getHomeRegion() == sim::EventRegion::Reactive)
      continue;
    std::optional<uint32_t> actorSlot = actorSlotFor(actor);
    IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
    auto analyzed = codeUnit ? analyzedActors.find(codeUnit.getUInt())
                             : analyzedActors.end();
    bool hasRuntimeCheckpointActivation =
        codeUnit &&
        llvm::any_of(runtimeCheckpointContinuations, [&](const auto &entry) {
          return entry.first == codeUnit.getUInt();
        });
    if (!actorSlot || analyzed == analyzedActors.end() ||
        (!isGeneratedRegionActor(actor) &&
         !::obelisk::schedule::has<::obelisk::schedule::Field::EvalBody>(
             actor) &&
         !hasRuntimeCheckpointActivation))
      continue;
    const SimulationProcessFrameAnalysis &frameAnalysis =
        *analyzed->second.analysis;
    if (auto evalBodyRef =
            ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
                actor)) {
      sim::SimFuncOp evalBody =
          designSymbols.lookup<sim::SimFuncOp>(evalBodyRef.getValue());
      if (!evalBody)
        return actor.emitOpError("references a missing eval body");
      auto continuation = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalContinuation>(evalBody);
      uint32_t continuationID = 0;
      ArrayRef<ProcessSuspension> suspensions = frameAnalysis.getSuspensions();
      if (suspensions.size() == 1)
        continuationID = suspensions.front().continuationID;
      else if (continuation && continuation.getInt() > 0 &&
               static_cast<uint64_t>(continuation.getInt()) <= UINT32_MAX &&
               llvm::any_of(suspensions, [&](const ProcessSuspension &site) {
                 return site.continuationID == continuation.getUInt();
               }))
        continuationID = static_cast<uint32_t>(continuation.getUInt());
      if (continuationID == 0)
        continue;
      auto bytecode = bytecodeContinuations.find(analyzed->second.operation);
      bool runtimeCheckpoint =
          codeUnit && runtimeCheckpointContinuations.contains(
                          {codeUnit.getUInt(), continuationID});
      runtimeCheckpoint |= ::obelisk::schedule::has<
          ::obelisk::schedule::Field::EvalCheckpointOnly>(evalBody);
      // IEEE 1800-2023 4.6(b), 10.4.2 require observable NBA updates to
      // retain their enqueue order. Keep this activation on the runtime
      // queue through a checkpoint; unrelated generated executors can still
      // own the rest of the eval plan.
      runtimeCheckpoint |=
          ::obelisk::schedule::has<evalRuntimeNBARequiredAttr>(evalBody);
      if (runtimeCheckpoint ||
          (bytecode != bytecodeContinuations.end() &&
           llvm::is_contained(bytecode->second, continuationID))) {
        if (runtimeCheckpoint) {
          SmallString<96> wrapperName;
          (Twine("__obelisk_direct_fragment_") + Twine(*actorSlot) + "_" +
           Twine(continuationID) + ".__obelisk_execute")
              .toVector(wrapperName);
          ContinuationFragmentCoverage coverage =
              fragmentCoverageFor(actor, continuationID);
          pendingEvalWrappers.push_back({actor,
                                         {},
                                         actor,
                                         wrapperName.str().str(),
                                         {},
                                         *actorSlot,
                                         continuationID,
                                         &frameAnalysis,
                                         /*initialActivation=*/false,
                                         std::move(coverage.members),
                                         /*runtimeCheckpoint=*/true});
        }
        continue;
      }
      // Keep derived MLIR metadata coherent for the two-state/checkpoint
      // variants cloned from this body later in the conversion.
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalContinuation>(
          evalBody,
          IntegerAttr::get(IntegerType::get(context, 32), continuationID));
      // A direct body has no coroutine frame arguments.  Continuations with
      // live block arguments (for example `repeat (N) @(posedge clk)`) must
      // remain runtime-owned until that finite control state is exhausted.
      // Treating a cloned body as their owner drops the loop-carried value and
      // can let run_until bypass reset/stimulus continuations entirely.
      bool continuationHasArguments = false;
      actor.walk([&](Operation *operation) {
        schedule::ContinuationSiteAttr site;
        if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
          site = suspend.getSiteAttr();
        if (site && site.getId() == continuationID &&
            operation->getNumSuccessors() == 1)
          continuationHasArguments |=
              operation->getSuccessor(0)->getNumArguments() != 0;
      });
      // Only a producer-certified generated region may reconstruct actor-side
      // continuation arguments from canonical state on every activation.
      // Ordinary or externally authored regions require their coroutine frame
      // to preserve such loop-carried values and cannot be claimed here.
      if (continuationHasArguments && !isGeneratedRegionActor(actor))
        continue;
      SmallString<96> wrapperName;
      (Twine("__obelisk_direct_fragment_") + Twine(*actorSlot) + "_" +
       Twine(continuationID))
          .toVector(wrapperName);
      std::string wrapper = (Twine(wrapperName) + ".__obelisk_execute").str();
      sim::SimFuncOp twoStateBody;
      std::string twoStateWrapper;
      if (auto variant =
              ::obelisk::schedule::get<schedule::metadata::evalTwoStateVariant>(
                  evalBody)) {
        twoStateBody = designSymbols.lookup<sim::SimFuncOp>(variant.getValue());
        if (!twoStateBody)
          return evalBody.emitOpError("references a missing two-state body");
        twoStateWrapper = wrapper + ".two_state";
      }
      ContinuationFragmentCoverage coverage =
          fragmentCoverageFor(actor, continuationID);
      pendingEvalWrappers.push_back(
          {evalBody, twoStateBody, actor, std::move(wrapper),
           std::move(twoStateWrapper), *actorSlot, continuationID,
           &frameAnalysis,
           /*initialActivation=*/false, std::move(coverage.members)});
      continue;
    }
    struct CurrentDirectWait {
      Operation *operation;
      Block *continuation;
      uint32_t continuationID;
    };
    bool generatedRegionBody = isGeneratedRegionActor(actor);
    SmallVector<CurrentDirectWait> directWaits;
    bool directWaitsSupported = true;
    actor.walk([&](Operation *operation) {
      if (!isa<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
               sim::SimSuspendAnyOp, sim::SimSuspendObserveOp>(operation))
        return;
      schedule::ContinuationSiteAttr site;
      if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation))
        site = suspend.getSiteAttr();
      else
        site = cast<sim::SimSuspendObserveOp>(operation).getSiteAttr();
      if (!site || site.getId() == 0 || operation->getNumSuccessors() != 1 ||
          (!generatedRegionBody &&
           operation->getSuccessor(0)->getNumArguments() != 0)) {
        directWaitsSupported = false;
        return;
      }
      directWaits.push_back(
          {operation, operation->getSuccessor(0), site.getId()});
    });
    if (!directWaitsSupported || directWaits.size() != 1)
      continue;
    for (const CurrentDirectWait &suspension : directWaits) {
      auto bytecode = bytecodeContinuations.find(analyzed->second.operation);
      bool runtimeCheckpoint =
          codeUnit && runtimeCheckpointContinuations.contains(
                          {codeUnit.getUInt(), suspension.continuationID});
      bool bytecodeContinuation =
          bytecode != bytecodeContinuations.end() &&
          llvm::is_contained(bytecode->second, suspension.continuationID);
      if (bytecodeContinuation && !runtimeCheckpoint)
        continue;

      if (runtimeCheckpoint) {
        SmallString<96> wrapperName;
        (Twine("__obelisk_direct_fragment_") + Twine(*actorSlot) + "_" +
         Twine(suspension.continuationID) + ".__obelisk_execute")
            .toVector(wrapperName);
        ContinuationFragmentCoverage coverage =
            fragmentCoverageFor(actor, suspension.continuationID);
        pendingEvalWrappers.push_back({actor,
                                       {},
                                       actor,
                                       wrapperName.str().str(),
                                       {},
                                       *actorSlot,
                                       suspension.continuationID,
                                       &frameAnalysis,
                                       /*initialActivation=*/false,
                                       std::move(coverage.members),
                                       /*runtimeCheckpoint=*/true});
        continue;
      }

      // A generated region kernel's entry path initializes its snapshot
      // arguments and evaluates the complete local region before reaching
      // suspend.any.  Clone that path as the direct Tier-2 activation; other
      // processes start at their ordinary resume continuation.
      Block *start = generatedRegionBody ? &actor.getBody().front()
                                         : suspension.continuation;
      auto isTerminalWait = [&](Block *block) {
        Operation *terminator = block->getTerminator();
        schedule::ContinuationSiteAttr site;
        if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(terminator))
          site = suspend.getSiteAttr();
        return site && site.getId() == suspension.continuationID;
      };
      SmallVector<Block *> blocks;
      SmallVector<Block *> pending{start};
      DenseSet<Block *> seen;
      bool supported = true;
      while (!pending.empty() && supported) {
        Block *block = pending.pop_back_val();
        if (!seen.insert(block).second)
          continue;
        if ((!generatedRegionBody && block == &actor.getBody().front()) ||
            block->getParent() != &actor.getBody()) {
          supported = false;
          break;
        }
        bool terminalWait = isTerminalWait(block);
        for (Operation &operation : *block) {
          if ((sim::isSuspensionOp(&operation) && !terminalWait) ||
              isa<sim::SimReturnOp, sim::SimDPICallOp>(operation)) {
            supported = false;
            break;
          }
          if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation))
            if (nba.getDelay() ||
                (nba.getSiteAttr() && nba.getSiteAttr().getTiming())) {
              supported = false;
              break;
            }
        }
        if (!supported)
          break;
        blocks.push_back(block);
        if (terminalWait)
          continue;
        Operation *terminator = block->getTerminator();
        if (!isa<cf::BranchOp, cf::CondBranchOp>(terminator)) {
          supported = false;
          break;
        }
        for (Block *successor : terminator->getSuccessors()) {
          pending.push_back(successor);
        }
      }
      // A direct fragment owns the complete acyclic activation CFG for its
      // physical continuation. Applying a local code-size cap here would
      // split one semantic owner between the generated coordinator and the
      // fine actor dispatcher, invalidating exclusive ownership.
      if (!supported || blocks.empty())
        continue;

      DenseSet<Block *> blockSet(blocks.begin(), blocks.end());
      for (Block *block : blocks)
        for (Operation &operation : *block)
          for (Value operand : operation.getOperands()) {
            Block *definition = operand.getParentBlock();
            bool entryArgument = isa<BlockArgument>(operand) &&
                                 definition == &actor.getBody().front();
            if (!entryArgument && !blockSet.contains(definition)) {
              supported = false;
              break;
            }
          }
      if (!supported)
        continue;

      SmallString<96> bodyName;
      (Twine("__obelisk_direct_fragment_") + Twine(*actorSlot) + "_" +
       Twine(suspension.continuationID))
          .toVector(bodyName);
      unsigned collision = 0;
      bodyName = SymbolTable::generateSymbolName<96>(
          bodyName,
          [&](StringRef name) { return designSymbols.lookup(name) != nullptr; },
          collision);
      SmallVector<DictionaryAttr> argumentAttrs;
      for (BlockArgument argument : actor.getBody().front().getArguments())
        argumentAttrs.push_back(actor.getArgAttrDict(argument.getArgNumber()));
      OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
      sim::SimFuncOp body = sim::SimFuncOp::create(
          builder, actor.getLoc(), bodyName,
          FunctionType::get(context, actor.getFunctionType().getInputs(),
                            TypeRange{}),
          sim::EntryKind::Function, ArrayRef<NamedAttribute>{}, argumentAttrs);
      designSymbols.insert(body);
      detail::copyNativePartition(actor, body);
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalBorrowedCaptures>(
          body, UnitAttr::get(context));
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRawCaptures>(
          body, UnitAttr::get(context));
      if (auto owners = ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalSourceOwners>(actor))
        ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
            body, owners);
      if (auto group = ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalFusionGroup>(actor))
        ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFusionGroup>(
            body, group);
      SymbolTable::setSymbolVisibility(body, SymbolTable::Visibility::Private);
      IRMapping mapping;
      for (auto [source, destination] :
           llvm::zip_equal(actor.getBody().front().getArguments(),
                           body.getBody().front().getArguments()))
        mapping.map(source, destination);
      for (Block *source : blocks) {
        if (source == &actor.getBody().front()) {
          mapping.map(source, &body.getBody().front());
          continue;
        }
        Block *destination = new Block;
        body.getBody().push_back(destination);
        mapping.map(source, destination);
        for (BlockArgument argument : source->getArguments()) {
          BlockArgument mapped =
              destination->addArgument(argument.getType(), argument.getLoc());
          mapping.map(argument, mapped);
        }
      }
      if (!generatedRegionBody) {
        builder.setInsertionPointToEnd(&body.getBody().front());
        cf::BranchOp::create(builder, actor.getLoc(), mapping.lookup(start));
      }
      for (Block *source : blocks) {
        builder.setInsertionPointToEnd(mapping.lookup(source));
        for (Operation &operation : *source) {
          if (isTerminalWait(source) && &operation == source->getTerminator())
            sim::SimReturnOp::create(builder, operation.getLoc(), ValueRange{});
          else
            builder.clone(operation, mapping);
        }
      }

      std::string wrapper = (Twine(bodyName) + ".__obelisk_execute").str();
      ContinuationFragmentCoverage coverage =
          fragmentCoverageFor(actor, suspension.continuationID);
      pendingEvalWrappers.push_back({body,
                                     {},
                                     actor,
                                     std::move(wrapper),
                                     {},
                                     *actorSlot,
                                     suspension.continuationID,
                                     &frameAnalysis,
                                     generatedRegionBody,
                                     std::move(coverage.members)});
    }
  }
  // Adding LLVM wrapper operations while walking the process-analysis map can
  // invalidate MLIR's internal symbol/cache state once enough eval bodies are
  // present.  Materialize them in a second phase after every actor decision is
  // complete.
  for (PendingEvalWrapper &pending : pendingEvalWrappers) {
    // Preserve the typed executor identity across design-to-module symbol
    // flattening. A fusion executor can own several physical source
    // continuations, so actor/continuation labels are not sufficient to decide
    // procedural self-suppression inside the generated clock-group closure.
    if (!pending.runtimeCheckpoint) {
      Builder identityBuilder(context);
      auto directFragmentAttr =
          identityBuilder.getI32IntegerAttr(result.size());
      for (sim::SimFuncOp body : {pending.body, pending.twoStateBody}) {
        if (!body)
          continue;
        if (auto previous = ::obelisk::schedule::get<
                ::obelisk::schedule::Field::EvalDirectFragment>(body);
            previous && previous != directFragmentAttr)
          return body.emitOpError(
              "is shared by multiple typed direct fragments");
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalDirectFragment>(body,
                                                            directFragmentAttr);
      }
    }
    if (pending.runtimeCheckpoint
            ? failed(makeRuntimeCheckpointWrapper(
                  module, pending.actor, pending.wrapper, pending.actorSlot,
                  pending.continuation))
            : failed(makeDirectFragmentWrapper(
                  module, pending.body, pending.actor, pending.wrapper,
                  pending.actorSlot, pending.continuation, *pending.analysis)))
      return failure();
    if (pending.twoStateBody && failed(makeDirectFragmentWrapper(
                                    module, pending.twoStateBody, pending.actor,
                                    pending.twoStateWrapper, pending.actorSlot,
                                    pending.continuation, *pending.analysis)))
      return failure();
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
            pending.body)) {
      Builder builder(context);
      checkpointRoutes.push_back(schedule::CheckpointRouteAttr::get(
          builder.getContext(),
          FlatSymbolRefAttr::get(context, pending.body.getSymName()),
          builder.getI32IntegerAttr(pending.actorSlot),
          builder.getI32IntegerAttr(pending.continuation)));
    }
    bool initialActivation = pending.initialActivation.value_or(false);
    SmallVector<NativePromotionRange> localPromotionRanges;
    if (pending.twoStateBody) {
      auto encoded = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalLocalPromotionRanges>(
          pending.twoStateBody);
      if (!encoded || (encoded.size() & 1) != 0)
        return pending.twoStateBody.emitOpError(
                   "has malformed local promotion ranges"),
               failure();
      ArrayRef<int64_t> values = encoded.asArrayRef();
      for (size_t index = 0; index != values.size(); index += 2) {
        if (values[index] < 0 || values[index + 1] <= 0)
          return pending.twoStateBody.emitOpError(
                     "has invalid local promotion range"),
                 failure();
        localPromotionRanges.push_back(
            {static_cast<uint64_t>(values[index]),
             static_cast<uint64_t>(values[index + 1])});
      }
      // An uncertified path-guarded body revalidates both its exact CFG and
      // known state on every activation. A known-preserving guarded body keeps
      // the complete range union above, allowing its owner bit to graduate
      // once without weakening the checkpoint-path guard.
      if (::obelisk::schedule::has<schedule::metadata::evalPathGuardedTwoState>(
              pending.twoStateBody) &&
          !::obelisk::schedule::has<
              schedule::metadata::evalPathGuardedKnownPreserving>(
              pending.twoStateBody))
        localPromotionRanges.clear();
    }
    SmallVector<std::pair<uint32_t, uint32_t>> sourceOwners;
    SmallVector<uint64_t> sourceCodeUnits;
    SmallVector<sim::SimFuncOp> ownerClosure{pending.body};
    llvm::SmallPtrSet<Operation *, 16> ownerClosureSeen;
    for (size_t index = 0; index != ownerClosure.size(); ++index) {
      sim::SimFuncOp function = ownerClosure[index];
      if (!ownerClosureSeen.insert(function.getOperation()).second)
        continue;
      function.walk([&](sim::SimCallOp call) {
        if (sim::SimFuncOp callee =
                designSymbols.lookup<sim::SimFuncOp>(call.getCallee()))
          ownerClosure.push_back(callee);
      });
    }
    auto collectSourceOwners = [&](sim::SimFuncOp function) -> LogicalResult {
      ArrayAttr owners = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalSourceOwners>(function);
      if (!owners)
        return success();
      for (Attribute attribute : owners) {
        auto owner = dyn_cast<schedule::SourceOwnerAttr>(attribute);
        auto codeUnit = owner ? owner.getCodeUnit() : IntegerAttr{};
        auto continuation = owner ? owner.getContinuation() : IntegerAttr{};
        if (!codeUnit || codeUnit.getInt() < 0 || !continuation ||
            continuation.getInt() <= 0 ||
            static_cast<uint64_t>(continuation.getInt()) > UINT32_MAX)
          return function.emitOpError("has malformed stable eval source owner");
        uint32_t continuationID = static_cast<uint32_t>(continuation.getInt());
        sourceCodeUnits.push_back(codeUnit.getUInt());
        auto physical = physicalActors.find(codeUnit.getUInt());
        if (physical == physicalActors.end())
          continue;
        ArrayRef<uint32_t> current = physical->second.continuations;
        if (!llvm::is_contained(current, continuationID)) {
          if (current.size() != 1)
            continue;
          continuationID = current.front();
        }
        sourceOwners.emplace_back(physical->second.slot, continuationID);
      }
      return success();
    };
    for (sim::SimFuncOp function : ownerClosure)
      if (failed(collectSourceOwners(function)))
        return failure();
    if (!ownerClosureSeen.contains(pending.actor.getOperation()) &&
        failed(collectSourceOwners(pending.actor)))
      return failure();
    llvm::sort(sourceOwners);
    sourceOwners.erase(std::unique(sourceOwners.begin(), sourceOwners.end()),
                       sourceOwners.end());
    llvm::sort(sourceCodeUnits);
    sourceCodeUnits.erase(
        std::unique(sourceCodeUnits.begin(), sourceCodeUnits.end()),
        sourceCodeUnits.end());
    uint32_t fusionGroup = UINT32_MAX;
    auto collectFusionGroup = [&](sim::SimFuncOp function) -> LogicalResult {
      auto group =
          ::obelisk::schedule::get<::obelisk::schedule::Field::EvalFusionGroup>(
              function);
      if (!group)
        return success();
      if (group.getInt() < 0 ||
          static_cast<uint64_t>(group.getInt()) > UINT32_MAX)
        return function.emitOpError("has an invalid eval fusion group");
      uint32_t value = static_cast<uint32_t>(group.getInt());
      if (fusionGroup != UINT32_MAX && fusionGroup != value)
        return function.emitOpError("crosses multiple eval fusion groups");
      fusionGroup = value;
      return success();
    };
    for (sim::SimFuncOp function : ownerClosure)
      if (failed(collectFusionGroup(function)))
        return failure();
    if (!ownerClosureSeen.contains(pending.actor.getOperation()) &&
        failed(collectFusionGroup(pending.actor)))
      return failure();
    result.push_back(
        {pending.actorSlot, pending.continuation,
         pending.body.getSymName().str(), std::move(pending.wrapper),
         std::move(pending.twoStateWrapper),
         pending.twoStateBody ? pending.twoStateBody.getSymName().str()
                              : std::string{},
         std::move(sourceOwners), std::move(sourceCodeUnits),
         std::move(pending.fragmentIDs), std::move(localPromotionRanges),
         fusionGroup,
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::EvalInstanceCoordinator>(pending.body),
         initialActivation});
  }
  if (!checkpointRoutes.empty())
    ::obelisk::schedule::set<schedule::metadata::evalCheckpointRoutes>(
        module, ArrayAttr::get(context, checkpointRoutes));
  return result;
}

LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  bool detailedTiming = module->hasAttr("obelisk.debug.native_timing");
  auto lastTiming = std::chrono::steady_clock::now();
  auto markTiming = [&](StringRef name) {
    if (!detailedTiming)
      return;
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - lastTiming).count();
    llvm::errs() << "obelisk native preparation timing: " << name << ": "
                 << seconds << " s\n";
    lastTiming = now;
  };
  // SimDesignOp is intentionally eliminated by this lowering. Preserve the
  // semantic partition inventory serially on the module before any early
  // bytecode-only or ordinary native path can erase its owner. String keys are
  // used because the design symbols themselves do not survive conversion.
  SmallVector<std::pair<std::string, ArrayAttr>> partitionManifests;
  module.walk([&](sim::SimDesignOp design) {
    if (ArrayAttr manifest = design->getAttrOfType<ArrayAttr>(
            sim::metadata::nativePartitionManifest))
      partitionManifests.emplace_back(design.getSymName().str(), manifest);
  });
  llvm::sort(partitionManifests, [](const auto &lhs, const auto &rhs) {
    return lhs.first < rhs.first;
  });
  if (partitionManifests.empty()) {
    module->removeAttr(sim::metadata::nativePartitionManifests);
  } else {
    Builder manifestBuilder(context);
    SmallVector<Attribute> preserved;
    preserved.reserve(partitionManifests.size());
    for (const auto &[design, partitions] : partitionManifests)
      preserved.push_back(manifestBuilder.getDictionaryAttr({
          manifestBuilder.getNamedAttr("design",
                                       manifestBuilder.getStringAttr(design)),
          manifestBuilder.getNamedAttr("partitions", partitions),
      }));
    module->setAttr(sim::metadata::nativePartitionManifests,
                    manifestBuilder.getArrayAttr(preserved));
  }
  if (failed(prepareManagedLowering(module, dataLayout)))
    return failure();
  FailureOr<NativeStateLayout> stateLayout = buildNativeStateLayout(module);
  if (failed(stateLayout))
    return failure();
  auto embeddedStateBits =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
  if (!embeddedStateBits ||
      embeddedStateBits.getValue().getZExtValue() != stateLayout->bitCount)
    return module.emitError(
        "prepared native state layout disagrees with embedded execution "
        "metadata");
  markTiming("managed lowering and state layout");
  schedule::StaticSpecializationAttr staticSpecialization;
  schedule::StaticSuperstepAttr staticSuperstep;
  SmallVector<schedule::ComputeNBACommitAttr> staticNBACommits;
  sim::SimDesignOp metadataDesign;
  module.walk([&](sim::SimDesignOp design) {
    metadataDesign = design;
    staticSuperstep =
        ::obelisk::schedule::get<schedule::metadata::staticSuperstep>(design);
  });
  auto executionFlags =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.flags");
  bool bytecodeOnly =
      executionFlags && (executionFlags.getValue().getZExtValue() &
                         OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
  if (bytecodeOnly) {
    uint64_t stateBytes = (stateLayout->bitCount + 7) / 8;
    constexpr uint64_t stateGuardBytes = sizeof(uint64_t);
    makeStatePlane(module, "__obelisk_state_value",
                   stateBytes + stateGuardBytes, false, *stateLayout);
    makeStatePlane(module, "__obelisk_state_unknown",
                   stateBytes + stateGuardBytes, true, *stateLayout);
    materializeNativeSchedulerGlobals(module);
    declareNativeRuntimeABI(module);
    if (failed(materializeDPIThunks(module)))
      return failure();

    sim::SimFuncOp root;
    bool multipleRoots = false;
    if (metadataDesign)
      for (sim::SimFuncOp function :
           metadataDesign.getBody().front().getOps<sim::SimFuncOp>()) {
        if (function.getEntryKind() != sim::EntryKind::RootInitializer)
          continue;
        multipleRoots |= static_cast<bool>(root);
        if (!root)
          root = function;
      }
    if (multipleRoots)
      return module.emitError(
          "bytecode-only design has multiple root processes");
    if (!root)
      return module.emitError("bytecode-only design has no root process");
    FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>> rootAnalysis =
        SimulationProcessFrameAnalysis::create(root, dataLayout);
    if (failed(rootAnalysis))
      return failure();
    FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
        analysis::SimulationScheduleAnalysis::compute(module);
    if (failed(scheduleRanks))
      return failure();
    NativeSchedulePlan rootSchedule;
    rootSchedule.initialRank =
        scheduleRanks->getEntryRank(root.getOperation()).value_or(0);
    for (const ProcessSuspension &suspension :
         (*rootAnalysis)->getSuspensions())
      rootSchedule.continuations.emplace_back(
          suspension.continuationID,
          scheduleRanks->getBlockRank(suspension.continuation).value_or(0));
    uint64_t stableID = root.getCodeUnitId().value_or(
        stableProcessID(root.getSymName()) &
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    SymbolTable embeddedSymbols(module);
    if (failed(makeProcessDescriptor(module, embeddedSymbols, root.getLoc(),
                                     root.getSymName(), stableID,
                                     **rootAnalysis)))
      return failure();
    detail::declareProcessSpawnRuntimeABI(module);
    SymbolTable helperSymbols(module);
    if (failed(makeProcessSpawnHelper(module, helperSymbols, root,
                                      **rootAnalysis, rootSchedule)))
      return failure();
    std::string rootSpawnName = root.getSymName().str();
    rootSpawnName += ".__obelisk_spawn";
    LLVM::LLVMFuncOp rootSpawn;
    metadataDesign.walk([&](LLVM::LLVMFuncOp function) {
      if (function.getSymName() == rootSpawnName)
        rootSpawn = function;
    });
    if (!rootSpawn)
      return module.emitError("bytecode-only root spawn helper is missing");
    rootSpawn->moveBefore(metadataDesign);
    if (failed(makeSchedulerMain(module, *stateLayout, false, false)))
      return failure();

    // The executable bodies are frozen in the design image. Keep only the
    // metadata-derived globals, root spawn shell, DPI callbacks, and scheduler
    // entry point for native lowering.
    metadataDesign.erase();
    return success();
  }
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(metadataDesign);
  // Resolved nets and driver contributions occupy the same canonical native
  // planes as storage.  With no external writer their fixed handles are
  // always safe to address directly; publication and resolution still flow
  // through the ordinary scheduler boundaries. Writable VPI retains guarded
  // net accesses when there are no language observers/overrides. Driver
  // contributions need a whole-resolution clean guard, not a per-root guard:
  // a forced net must retain subsequent unforced driver updates for release.
  bool hasLanguageOverride = false;
  bool hasDynamicLanguageOverride = false;
  bool hasLanguageObserver = false;
  module.walk([&](Operation *operation) {
    hasLanguageOverride |= isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
                               sim::SimReleaseOverrideOp>(operation);
    hasDynamicLanguageOverride |= isa<sim::SimDynamicOverrideOp>(operation);
    if (auto function = dyn_cast<sim::SimFuncOp>(operation))
      hasLanguageObserver |=
          function.getEntryKind() == sim::EntryKind::Observer;
  });
  if (!hasLanguageOverride && (!vpi.allowsWrite() || !hasLanguageObserver)) {
    auto authorizeFixedHandles = [&](const auto &descriptors) {
      for (const auto &[descriptor, handle] : descriptors) {
        (void)descriptor;
        obelisk_rt_stable_handle_v1 decoded{};
        if (obelisk_rt_stable_handle_decode(handle, &decoded) &&
            decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
            decoded.offset == 0)
          (vpi.allowsWrite() ? stateLayout->guardedHandles
                             : stateLayout->directHandles)
              .insert(decoded.id);
      }
    };
    authorizeFixedHandles(stateLayout->nets);
    if (!vpi.allowsWrite())
      authorizeFixedHandles(stateLayout->drivers);
  }
  if (staticSuperstep &&
      (!metadataDesign || staticSuperstep.getSourceGraph() !=
                              metadataDesign.getComputeGraphAttr()))
    return module.emitError(
        "native lowering rejected stale static-superstep metadata");
  if (metadataDesign) {
    FailureOr<analysis::StaticSpecializationAnalysis> analyzed =
        analysis::StaticSpecializationAnalysis::compute(metadataDesign);
    if (failed(analyzed))
      return failure();
    staticSpecialization = analyzed->getPlan();
    llvm::append_range(staticNBACommits, analyzed->getOrderedNBACommits());
    DenseSet<uint64_t> plannedNBARoots;
    for (const auto &[descriptor, root] : analyzed->getRoots()) {
      if (!root.getDirect() && !root.getGuarded() && !root.getNba())
        continue;
      if (root.getWidth() == 0)
        return module.emitError(
            "native lowering rejected invalid static-specialization root");
      auto handle = stateLayout->storage.find(descriptor);
      if (handle == stateLayout->storage.end())
        return module.emitError(
            "static-specialization root references unknown storage");
      obelisk_rt_stable_handle_v1 decoded{};
      if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return module.emitError(
            "static-specialization root has an invalid native handle");
      auto bound = llvm::find_if(stateLayout->bounds, [&](const auto &entry) {
        return entry.handleID == decoded.id;
      });
      if (bound == stateLayout->bounds.end() || bound->width != root.getWidth())
        return module.emitError(
            "static-specialization root disagrees with native state layout");
      if (root.getDirect())
        stateLayout->directHandles.insert(decoded.id);
      if (root.getGuarded())
        stateLayout->guardedHandles.insert(decoded.id);
      if (root.getNba()) {
        plannedNBARoots.insert(descriptor);
        stateLayout->nbaHandles.insert(decoded.id);
      }
    }
    if (analyzed->getNBARoots().size() != plannedNBARoots.size())
      return module.emitError(
          "static-specialization NBA root policies disagree with the "
          "ordered inventory");
  }
  schedule::NativeSchedulerMode nativeScheduler =
      schedule::NativeSchedulerMode::Auto;
  if (auto mode =
          ::obelisk::schedule::get<::obelisk::schedule::Field::NativeScheduler>(
              module))
    nativeScheduler = mode.getValue();
  analysis::NativeAOTAnalysis aotEligibility;
  bool useAOT = false;
  bool evalScheduler = nativeScheduler == schedule::NativeSchedulerMode::Eval;
  DenseMap<Operation *, SmallVector<uint32_t>> aotBytecodeContinuations;
  DenseSet<std::pair<uint64_t, uint32_t>> runtimeCheckpointContinuations;
  DenseSet<uint64_t> checkpointOnlyActors;
  uint64_t stateBytes = (stateLayout->bitCount + 7) / 8;
  // Generated scalar root commits use an unaligned 64-bit window. Keep one
  // zeroed guard word after the canonical packed plane so a final narrow root
  // can use the same branch-free load/store sequence without crossing the
  // allocation. The public state bit count and snapshots exclude this padding.
  constexpr uint64_t stateGuardBytes = sizeof(uint64_t);
  makeStatePlane(module, "__obelisk_state_value", stateBytes + stateGuardBytes,
                 false, *stateLayout);
  makeStatePlane(module, "__obelisk_state_unknown",
                 stateBytes + stateGuardBytes, true, *stateLayout);
  materializeNativeSchedulerGlobals(module);
  declareNativeRuntimeABI(module);
  llvm::MapVector<Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    bool suspendable = false;
    function.walk([&](Operation *operation) {
      suspendable |= sim::isSuspensionOp(operation);
    });
    bool process = function.getEntryKind() != sim::EntryKind::Function &&
                   function.getEntryKind() != sim::EntryKind::Observer;
    if (failed(insertAutomaticOwnerReleases(function)))
      return WalkResult::interrupt();
    if (suspendable && failed(threadProcessStateThroughCFG(function)))
      return WalkResult::interrupt();
    // Zero-time functions can contain process-control terminators (for
    // example `process::kill`) without becoming suspendable actors. Runtime
    // status threading propagates those control effects through their call
    // chain. Frame-analyzing them here would retain a second owner for the
    // function after ordinary lowering erases it.
    if (!process)
      return WalkResult::advance();
    auto analysis =
        SimulationProcessFrameAnalysis::create(function, dataLayout);
    if (failed(analysis))
      return WalkResult::interrupt();
    for (const ProcessSuspension &suspension : (*analysis)->getSuspensions()) {
      suspension.operation->setAttr(
          "obelisk.coro.continuation",
          IntegerAttr::get(IntegerType::get(context, 32),
                           suspension.continuationID));
      suspension.operation->setAttr(
          "obelisk.coro.wait_offset",
          IntegerAttr::get(IntegerType::get(context, 64),
                           suspension.waitOffset));
      suspension.operation->setAttr(
          "obelisk.coro.wait_size",
          IntegerAttr::get(IntegerType::get(context, 64), suspension.waitSize));
    }
    analyses.insert({function.getOperation(), std::move(*analysis)});
    return WalkResult::advance();
  });
  if (analyzed.wasInterrupted())
    return failure();
  markTiming("frame analysis and state threading");
  // AOT planning only reads design symbols. Share their lookup table across
  // actor/body and transitive-call queries. Do not reuse it after lowering
  // creates or replaces function symbols.
  SymbolTableCollection planningSymbols;
  // Fixed root-spawn captures are useful independently of scheduler
  // selection: replacing a proven-unique storage capture with its context
  // lookup exposes a constant stable handle to direct-state lowering.  The
  // AOT analysis also records dynamic/duplicate actors, so the same proof is
  // safe for the generic scheduler.
  constexpr StringLiteral runtimePublicationCertificate =
      "obelisk.runtime_publication_certified";
  module.walk([&](sim::SimFuncOp function) {
    function->removeAttr(runtimePublicationCertificate);
  });
  aotEligibility = analysis::NativeAOTAnalysis::compute(module);
  for (const auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    IntegerAttr codeUnit =
        function ? function.getCodeUnitIdAttr() : IntegerAttr{};
    if (!function || !codeUnit)
      continue;
    // Preserve the full Clause 31.9.1 structural proof across later CFG and
    // coroutine rewrites. Input IR cannot forge this internal certificate: it
    // is cleared above and recreated only after the exact commit audit.
    if (analysis::isNegativeTimingDelayCommit(function))
      function->setAttr(runtimePublicationCertificate, UnitAttr::get(context));
  }
  for (Operation *operation : aotEligibility.getRuntimeObservedWriterActors()) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(operation);
    IntegerAttr codeUnit =
        function ? function.getCodeUnitIdAttr() : IntegerAttr{};
    auto analyzed = analyses.find(operation);
    if (!function || !codeUnit || analyzed == analyses.end())
      return module.emitError(
                 "runtime-observed source writer has no process analysis"),
             failure();
    // IEEE 1800-2017 Clauses 31.7 and 31.9.1 require runtime-owned timing
    // observers to see their primary/source publication in the same scheduler
    // cohort. Keep every activation of an overlapping writer behind an exact
    // cold checkpoint so the generic scheduler performs the publication and
    // wakeup before the generated island is retried.
    for (const ProcessSuspension &suspension :
         analyzed->second->getSuspensions())
      runtimeCheckpointContinuations.insert(
          {codeUnit.getUInt(), suspension.continuationID});
    checkpointOnlyActors.insert(codeUnit.getUInt());
  }
  // SimFunc operations may be rebuilt by two-state specialization and packed
  // lowering. Preserve the analysis actor identity as a stable code-unit join
  // instead of retaining Operation pointers across those rewrite boundaries.
  DenseMap<uint64_t, uint32_t> aotActorSlotsByCodeUnit;
  for (const auto &[operation, slot] : aotEligibility.getActorSlots()) {
    auto actor = dyn_cast_if_present<sim::SimFuncOp>(operation);
    IntegerAttr codeUnit = actor ? actor.getCodeUnitIdAttr() : IntegerAttr{};
    if (!codeUnit)
      continue;
    auto [found, inserted] =
        aotActorSlotsByCodeUnit.try_emplace(codeUnit.getUInt(), slot);
    if (!inserted && found->second != slot)
      return actor.emitOpError("has a duplicate AOT code-unit identity");
  }
  auto aotActorSlotFor = [&](sim::SimFuncOp actor) -> std::optional<uint32_t> {
    IntegerAttr codeUnit = actor ? actor.getCodeUnitIdAttr() : IntegerAttr{};
    if (!codeUnit)
      return std::nullopt;
    auto found = aotActorSlotsByCodeUnit.find(codeUnit.getUInt());
    if (found == aotActorSlotsByCodeUnit.end())
      return std::nullopt;
    return found->second;
  };
  if (nativeScheduler != schedule::NativeSchedulerMode::Generic) {
    bool forcedAOT =
        nativeScheduler == schedule::NativeSchedulerMode::AOT || evalScheduler;
    useAOT = aotEligibility.isEligible() &&
             (forcedAOT || aotEligibility.isAOTCostEffective());
    if (forcedAOT && !aotEligibility.isFullyEligible() &&
        !aotEligibility.isForcedHybridEligible()) {
      InFlightDiagnostic diagnostic =
          module.emitError("design is ineligible for native AOT scheduling: ");
      if (aotEligibility.getReasons().empty())
        diagnostic << "no statically schedulable process actors";
      else
        llvm::interleaveComma(aotEligibility.getReasons(), diagnostic);
      return failure();
    }
  }
  bool cleanSuperstep = false;
  // IEEE 1800-2017 16.14 and Clause 31 coordinators deliberately retain cohort
  // ordering in a runtime-owned actor. Forced-hybrid eligibility alone also
  // covers other cold assertion shapes, so only matching static-superstep
  // metadata certifies a closed native eval island.
  bool certifiedStaticSuperstep = false;
  if (staticSuperstep && useAOT) {
    ArrayAttr actors = staticSuperstep.getActors();
    if (actors.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "native lowering rejected stale static-superstep actor inventory");
    for (auto [slot, attribute] : llvm::enumerate(actors)) {
      auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
      sim::SimFuncOp function =
          actor ? planningSymbols.lookupSymbolIn<sim::SimFuncOp>(metadataDesign,
                                                                 actor)
                : nullptr;
      auto planned =
          function
              ? aotEligibility.getActorSlots().find(function.getOperation())
              : aotEligibility.getActorSlots().end();
      if (!function || planned == aotEligibility.getActorSlots().end() ||
          planned->second != slot)
        return module.emitError(
            "native lowering rejected stale static-superstep actor order");
    }
    certifiedStaticSuperstep = true;
  }
  // The legacy hybrid AOT scheduler must retain generic fanout for its cold
  // coordinator. Only eval has the explicit island ABI and periodic overlap
  // guard needed to execute the residual closure directly. Fully eligible
  // designs retain independent static capabilities even when a focused
  // conversion pipeline did not run the optional superstep planner.
  bool staticEvalIsland =
      certifiedStaticSuperstep &&
      (evalScheduler ||
       nativeScheduler == schedule::NativeSchedulerMode::Auto) &&
      !aotEligibility.isFullyEligible();
  bool closedStaticIsland =
      aotEligibility.isFullyEligible() || staticEvalIsland;
  cleanSuperstep = certifiedStaticSuperstep && closedStaticIsland;
  if (aotEligibility.isEligible() &&
      failed(specializeNativeAOTCaptures(module, aotEligibility)))
    return failure();
  bool staticControl = false;
  bool staticFanout = false;
  bool staticFanoutMetadata = false;
  bool directStaticState = false;
  bool staticNBA = false;
  NativeStaticNBAPlan staticNBAPlan;
  NativeStaticFanoutPlan staticFanoutPlan;
  SmallVector<NativePeriodicClock> periodicClocks;
  SmallVector<NativePeriodicAlias> periodicAliases;
  NativeThreeTierPlan threeTierPlan;
  SmallVector<obelisk_rt_static_actor_root> staticActorRoots;
  // Direct static state is an addressing capability, not a scheduler
  // capability.  The specialization analysis has already proved each root's
  // fixed descriptor, width, and native-plane offset.  Make those facts
  // available to generic and hybrid lowering as well; dynamic handles still
  // use the validating runtime helpers and writable VPI roots retain their
  // generated guards.
  // Read-only VPI is a reflection capability, not an always-live observer.
  // Explicit VPI/DPI calls are safe points and design reads consult the
  // generated plan's canonical plane directly. Full VPI uses guarded
  // specialization. Ordinary force/release targets already have guarded root
  // policies; an unresolved target guards every storage root. Preserve those
  // exact policies instead of disabling unrelated direct accesses. IEEE
  // 1800-2023 10.6.2 still requires forced writes and release to use the runtime
  // path, including retained continuous values. Dynamic override ownership is
  // not covered by the static root inventory and remains conservative. The
  // separate net/driver authorization above retains its resolution barrier.
  directStaticState = staticSpecialization && vpi.hasComputeGraph() &&
                      !hasDynamicLanguageOverride &&
                      (!stateLayout->directHandles.empty() ||
                       !stateLayout->guardedHandles.empty());
  if (useAOT && closedStaticIsland) {
    staticControl = vpi.hasComputeGraph();
    staticFanoutMetadata = vpi.hasComputeGraph();
    // Read-only VPI observes the same canonical planes but cannot mutate
    // roots or invalidate the closed-world waiter inventory. It therefore
    // uses the fully static fanout schedule just like VPI-off.
    staticFanout = vpi.preservesStaticDependencies();
    staticNBA = staticSpecialization && !stateLayout->nbaHandles.empty();
  }
  if (staticControl) {
    module.walk([&](sim::SimFuncOp function) {
      if (!aotEligibility.getActorSlots().contains(function.getOperation()))
        return;
      function.walk([&](Operation *operation) {
      if (llvm::any_of(operation->getOperandTypes(),
                       [](Type type) { return isa<FloatType>(type); }) ||
          llvm::any_of(operation->getResultTypes(),
                       [](Type type) { return isa<FloatType>(type); })) {
        staticControl = false;
        staticFanout = false;
        staticFanoutMetadata = false;
      }
      });
    });
  }
  if (staticFanoutMetadata) {
    FailureOr<NativeStaticFanoutPlan> fanout = buildNativeStaticFanoutPlan(
        module, *stateLayout, aotEligibility.getActorSlots(),
        aotEligibility.getBytecodeFragments(),
        aotEligibility.getRuntimeOwnedFanoutActors(), true, staticEvalIsland);
    if (failed(fanout))
      return failure();
    staticFanoutPlan = std::move(*fanout);
    staticFanoutMetadata &= staticFanoutPlan.exact;
    staticFanout &= staticFanoutPlan.exact;
    if (staticFanoutPlan.exact) {
      stateLayout->transitionHandlesExact = true;
      for (uint32_t staticState : staticFanoutPlan.runtimeTransitionStates)
        stateLayout->transitionHandles.insert(staticState);
      for (const obelisk_rt_static_fanout_entry &entry :
           staticFanoutPlan.entries)
        stateLayout->transitionHandles.insert(entry.static_state);
      // Toggle coverage observes every committed transition even when the
      // exact language-level fanout is empty. Keep a notification at covered
      // roots; the runtime's static fast path records it before its fanout-only
      // early return.
      module.walk([&](Operation *operation) {
        if (!operation->hasAttr(sim::metadata::coverageToggleObservable))
          return;
        const uint64_t *handle = nullptr;
        if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
          auto found = stateLayout->storage.find(storage.getId());
          if (found != stateLayout->storage.end())
            handle = &found->second;
        } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
          auto found = stateLayout->nets.find(net.getId());
          if (found != stateLayout->nets.end())
            handle = &found->second;
        }
        if (!handle)
          return;
        obelisk_rt_stable_handle_v1 decoded{};
        if (obelisk_rt_stable_handle_decode(*handle, &decoded) &&
            decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC)
          stateLayout->transitionHandles.insert(decoded.id);
      });
    }
  }
  // State, NBA, and fanout are independent capabilities. Direct access is
  // selected per operation by resolveDirectStaticStateRange; a wide or
  // otherwise generic root does not prevent an independent narrow root from
  // using generated planes.
  if (nativeScheduler == schedule::NativeSchedulerMode::Auto &&
      metadataDesign) {
    // An unpromoted automatic reference needs a runtime activation frame
    // (IEEE 1800-2023 6.21). Keep its complete owner at a checkpoint before
    // certifying NBA ownership; a helper's local packed temporary must not
    // introduce allocation/load calls into the runtime-free eval closure.
    analysis::DescriptorProvenanceAnalysis provenanceAnalysis(metadataDesign);
    llvm::DenseSet<uint64_t> runtimeObservedNets;
    for (const auto &net : stateLayout->netLayouts)
      if (staticFanoutPlan.runtimeTransitionStates.contains(net.handleID))
        runtimeObservedNets.insert(net.id);
    for (const auto &[canonical, component] :
         stateLayout->connectivityComponents) {
      (void)canonical;
      if (llvm::any_of(component, [&](const auto &bit) {
            return runtimeObservedNets.contains(bit.net);
          }))
        for (const auto &bit : component)
          runtimeObservedNets.insert(bit.net);
    }
    DenseMap<Operation *, bool> runtimeStateFunctions;
    metadataDesign.walk([&](sim::SimFuncOp actor) {
      if (!aotActorSlotFor(actor))
        return;
      auto body =
          ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(actor);
      sim::SimFuncOp function =
          body ? planningSymbols.lookupSymbolIn<sim::SimFuncOp>(metadataDesign,
                                                                 body)
               : sim::SimFuncOp{};
      if (!function)
        return;
      bool runtimeLocal = false;
      SmallVector<sim::SimFuncOp> pending{function};
      llvm::SmallPtrSet<Operation *, 8> visited;
      while (!pending.empty()) {
        sim::SimFuncOp current = pending.pop_back_val();
        if (!visited.insert(current.getOperation()).second)
          continue;
        auto [classification, inserted] =
            runtimeStateFunctions.try_emplace(current.getOperation(), false);
        if (inserted) {
          auto provenance = provenanceAnalysis.derive(current);
          auto runtimeStore = [&](Value destination,
                                  bool requireStaticAccess = true,
                                  bool requireStaticNBA = false) {
            auto found = provenance.find(destination);
            if (found == provenance.end() || !found->second.descriptor ||
                (requireStaticAccess && found->second.dynamic))
              return true;
            // Runtime-owned waiters need publication while the original
            // actor identity is active (IEEE 1800-2023 9.4.2). Generated
            // ingress alone cannot wake that part of the fanout.
            // Driver provenance is normalized to its net descriptor;
            // include aliases whose resolved transition wakes a waiter.
            if (found->second.resource == schedule::ComputeResourceKind::Net)
              return runtimeObservedNets.contains(*found->second.descriptor);
            const auto &handles = stateLayout->storage;
            auto handle = handles.find(*found->second.descriptor);
            obelisk_rt_stable_handle_v1 decoded{};
            return handle != handles.end() &&
                   obelisk_rt_stable_handle_decode(handle->second, &decoded) &&
                   decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
                   (staticFanoutPlan.runtimeTransitionStates.contains(
                        decoded.id) ||
                    (requireStaticNBA &&
                     !stateLayout->nbaHandles.contains(decoded.id)));
          };
          current.walk([&](Operation *operation) {
            // Dynamic blocking stores still lower through the runtime's
            // bounded, override-aware plane API. Preserve the complete
            // source activation at a checkpoint (IEEE 1800-2023 4.6(a),
            // 9.4.2, 11.5.1), including its transition publications.
            if (isa<sim::SimRefAllocOp>(operation))
              classification->second = true;
            else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
              classification->second |= runtimeStore(store.getReference());
            else if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation))
              classification->second |= runtimeStore(copy.getDestination());
            else if (auto store = dyn_cast<sim::SimNetWriteOp>(operation))
              classification->second |= runtimeStore(store.getNet());
            else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation))
              classification->second |= runtimeStore(drive.getDriver());
            else if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
              // LRM 4.6(b), 10.4.2: roots excluded from static NBA
              // specialization (including delayed/immediate mixtures)
              // keep their complete owner at the ordered runtime queue.
              classification->second |=
                  staticEvalIsland &&
                  runtimeStore(enqueue.getDestination(), false, true);
            }
          });
        }
        runtimeLocal |= classification->second;
        current.walk([&](sim::SimCallOp call) {
          if (sim::SimFuncOp callee =
                  planningSymbols.lookupSymbolIn<sim::SimFuncOp>(
                      metadataDesign, call.getCalleeAttr()))
            pending.push_back(callee);
        });
      }
      if (runtimeLocal)
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalCheckpointOnly>(
            function, UnitAttr::get(module.getContext()));
    });
    bool hasObserver = false;
    bool hasInactiveDelay = false;
    metadataDesign.walk([&](sim::SimFuncOp function) {
      hasObserver |= function.getEntryKind() == sim::EntryKind::Observer;
    });
    metadataDesign.walk([&](sim::SimSuspendDelayOp delay) {
      auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
      hasInactiveDelay |= !constant || constant.getValue() == 0;
    });
    auto execution =
        module->getAttrOfType<IntegerAttr>("obelisk.execution.flags");
    uint64_t flags = execution ? execution.getUInt() : 0;
    // IEEE 1800-2023 4.4, 4.6, 9.4.2: a runtime clock consumer prevents
    // exclusive calendar ownership, not execution of an independent closure.
    // The writer checkpoints above retain every required runtime publication;
    // final call-closure verification certifies the remaining generated work.
    // A coordinator currently drains its NBA queue as one transaction. A
    // zero/dynamic delay could require an intervening Inactive region, so
    // retain the original runtime path for those designs (LRM 4.4-4.5).
    if (staticEvalIsland && staticFanoutPlan.exact && !hasObserver &&
        !hasInactiveDelay &&
        !(flags & (OBELISK_RT_EXECUTION_VPI_READ |
                   OBELISK_RT_EXECUTION_VPI_WRITE |
                   OBELISK_RT_EXECUTION_DPI_EXPORTS |
                   OBELISK_RT_EXECUTION_COVERAGE_SCHEMA)))
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRuntimeCalendar>(
          module, UnitAttr::get(module.getContext()));
  }
  if (staticNBA) {
    FailureOr<NativeStaticNBAPlan> plan =
        buildNativeStaticNBAPlan(module, *stateLayout, staticNBACommits, true);
    if (failed(plan))
      return failure();
    staticNBAPlan = std::move(*plan);
    // IEEE 1800-2023 4.4.2.4, 4.5, 4.6(b), 10.4.2: a runtime calendar
    // shares the NBA barrier with generated Active work. Preserve execution
    // order whenever intermediate updates are observable (9.4.2).
    // Scalar merge-safe accumulators are visible to that barrier, so their
    // writers need no checkpoint. Wide eval latches and ordered queues are
    // private to the generated barrier and retain runtime ownership here.
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalRuntimeCalendar>(module))
      for (auto [index, root] : llvm::enumerate(staticNBAPlan.roots))
        if (root.bit_width > 64)
          staticNBAPlan.mergeSafeRoots[index] = false;
    staticNBA = !staticNBAPlan.roots.empty();
    // The generated queue orders all executions of its certified NBA sites,
    // splitting wide payloads into records. Before packed lowering, retain a
    // runtime owner if another site on its root has no admitted Eval body or
    // a payload cannot be queued. A late Eval decline cannot restore the
    // original per-update runtime publications.
    SmallVector<llvm::SmallDenseSet<uint64_t, 4>> generatedOrigins(
        staticNBAPlan.roots.size());
    SmallVector<uint8_t> queuePayloadSupported(staticNBAPlan.roots.size(), 1);
    auto semanticOrigin = [&](uint64_t site) {
      auto origin = staticNBAPlan.siteSemanticOrigins.find(site);
      return origin == staticNBAPlan.siteSemanticOrigins.end() ? site
                                                               : origin->second;
    };
    SmallVector<sim::SimFuncOp> admittedBodies;
    llvm::SmallPtrSet<Operation *, 8> mixedTierBodies;
    if (metadataDesign)
      metadataDesign.walk([&](sim::SimFuncOp actor) {
        if (!aotActorSlotFor(actor))
          return;
        auto body =
            ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
                actor);
        if (body)
          if (sim::SimFuncOp function =
                  planningSymbols.lookupSymbolIn<sim::SimFuncOp>(metadataDesign,
                                                                 body)) {
            admittedBodies.push_back(function);
            auto bytecode = aotEligibility.getBytecodeFragments().find(
                actor.getOperation());
            if (bytecode != aotEligibility.getBytecodeFragments().end() &&
                !bytecode->second.empty())
              mixedTierBodies.insert(function.getOperation());
          }
      });
    SmallVector<llvm::SmallDenseSet<uint32_t, 4>> bodyWideRoots(
        admittedBodies.size());
    SmallVector<bool> bodyNeedsOrderedNBA(admittedBodies.size(), false);
    for (auto [index, function] : llvm::enumerate(admittedBodies)) {
      SmallVector<sim::SimFuncOp> pending{function};
      llvm::SmallPtrSet<Operation *, 8> visited;
      while (!pending.empty()) {
        sim::SimFuncOp current = pending.pop_back_val();
        if (!visited.insert(current.getOperation()).second)
          continue;
        current.walk([&](sim::SimNBAEnqueueOp enqueue) {
          schedule::NBASiteAttr site = enqueue.getSiteAttr();
          auto root = site ? staticNBAPlan.siteRoots.find(site.getId())
                           : staticNBAPlan.siteRoots.end();
          if (root == staticNBAPlan.siteRoots.end() ||
              root->second >= generatedOrigins.size())
            return;
          // The generated accumulator publishes one old-to-final transition.
          // An activation with observable intermediate writes needs a closed
          // ordered queue or a runtime checkpoint throughout.
          bodyNeedsOrderedNBA[index] |=
              !staticNBAPlan.mergeSafeRoots[root->second];
          if (staticNBAPlan.roots[root->second].bit_width > 64)
            bodyWideRoots[index].insert(root->second);
          if (current == function) {
            generatedOrigins[root->second].insert(semanticOrigin(site.getId()));
            if (mixedTierBodies.contains(function.getOperation()))
              queuePayloadSupported[root->second] = 0;
          } else
            // Shared helpers have no single generated owner. The LLVM
            // preflight also declines them, so decide before packed lowering.
            queuePayloadSupported[root->second] = 0;
        });
        if (metadataDesign)
          current.walk([&](sim::SimCallOp call) {
            if (sim::SimFuncOp callee =
                    planningSymbols.lookupSymbolIn<sim::SimFuncOp>(
                        metadataDesign, call.getCalleeAttr()))
              pending.push_back(callee);
          });
      }
    }
    // The queue record carries a dynamic bit offset, so an array element of
    // a captured root is as addressable as a fixed slice. A packed dynamic
    // slice still needs a generated owner for its variable width mask.
    auto fixedReference = [](Value destination) {
      while (destination) {
        if (destination.getDefiningOp<sim::SimContextStorageOp>())
          return true;
        if (auto argument = dyn_cast<BlockArgument>(destination)) {
          auto function =
              dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
          return function && argument.getOwner()->isEntryBlock();
        }
        if (auto element =
                destination.getDefiningOp<sim::SimRefArrayElementOp>()) {
          destination = element.getInput();
          continue;
        }
        if (auto extract = destination.getDefiningOp<sim::SimRefExtractOp>()) {
          destination = extract.getInput();
          continue;
        }
        if (auto subelement =
                destination.getDefiningOp<sim::SimRefSubelementOp>()) {
          destination = subelement.getInput();
          continue;
        }
        return false;
      }
      return false;
    };
    module.walk([&](sim::SimNBAEnqueueOp enqueue) {
      schedule::NBASiteAttr site = enqueue.getSiteAttr();
      auto root = site ? staticNBAPlan.siteRoots.find(site.getId())
                       : staticNBAPlan.siteRoots.end();
      if (root == staticNBAPlan.siteRoots.end() ||
          root->second >= queuePayloadSupported.size())
        return;
      auto width = detail::nativeStateWidth(enqueue.getValue().getType());
      // Queue staging splits the enclosing block, which a structured
      // single-block region such as scf.for cannot hold. The queue drains at
      // the NBA barrier only; a reactive-set writer commits in Re-NBA, which
      // stays runtime scheduled. Both must be decided here, since a later
      // Eval decline cannot restore the per-update runtime publications. A
      // payload wider than one record is staged as consecutive 64-bit chunks
      // within the bounds of the chunk-site encoding (evalNBAChunkSite).
      auto function = enqueue->getParentOfType<sim::SimFuncOp>();
      if (!width || *width == 0 ||
          (*width > 64 && (site.getId() >= (uint64_t{1} << 48) ||
                           (*width + 63) / 64 >= (uint64_t{1} << 15))) ||
          enqueue.getDelay() ||
          site.getTiming() || !function ||
          function.getHomeRegion() != sim::EventRegion::Active ||
          enqueue->getParentRegion() != &function.getBody() ||
          enqueue.getDestination().getDefiningOp<sim::SimRefDynExtractOp>() ||
          (!staticNBAPlan.independentSiteWrites[root->second] &&
           !fixedReference(enqueue.getDestination())))
        queuePayloadSupported[root->second] = 0;
    });
    SmallVector<uint8_t> orderedRootClosed(staticNBAPlan.roots.size(), 1);
    for (const obelisk_rt_static_nba_site &site : staticNBAPlan.sites)
      if (site.root < orderedRootClosed.size() &&
          !generatedOrigins[site.root].contains(semanticOrigin(site.site)))
        orderedRootClosed[site.root] = 0;
    // IEEE 1800-2023 4.6(b), 10.4.2: the ordered queue must hold every
    // update whose order is observable.
    // A merge-safe root keeps its accumulator and cannot reveal that order.
    bool everySiteGenerated = !::obelisk::schedule::has<
        ::obelisk::schedule::Field::EvalRuntimeCalendar>(module);
    module.walk([&](sim::SimNBAEnqueueOp enqueue) {
      schedule::NBASiteAttr site = enqueue.getSiteAttr();
      auto root = site ? staticNBAPlan.siteRoots.find(site.getId())
                       : staticNBAPlan.siteRoots.end();
      if (root != staticNBAPlan.siteRoots.end() &&
          root->second < staticNBAPlan.mergeSafeRoots.size() &&
          staticNBAPlan.mergeSafeRoots[root->second])
        return;
      bool supported =
          root != staticNBAPlan.siteRoots.end() &&
          root->second < orderedRootClosed.size() &&
          orderedRootClosed[root->second] &&
          queuePayloadSupported[root->second];
      if (!supported && detailedTiming) {
        auto function = enqueue->getParentOfType<sim::SimFuncOp>();
        llvm::errs() << "ordered NBA boundary: function="
                     << (function ? function.getSymName() : StringRef("?"))
                     << " site=" << (site ? site.getId() : UINT64_MAX)
                     << " root="
                     << (root == staticNBAPlan.siteRoots.end() ? UINT32_MAX
                                                               : root->second)
                     << '\n';
      }
      everySiteGenerated &= supported;
    });
    // Wide roots have no scalar accumulator. Even merge-safe roots need a
    // closed queue here; a runtime initializer can otherwise escape the NBA
    // ownership proof. Moving one owner to a checkpoint also opens its other
    // roots, so propagate the boundary before lowering any publications
    // (IEEE 1800-2023 4.6(b), 10.4.2).
    bool addedRuntimeOwner;
    do {
      addedRuntimeOwner = false;
      for (auto [index, function] : llvm::enumerate(admittedBodies)) {
        bool runtimeOwner =
            ::obelisk::schedule::has<evalRuntimeNBARequiredAttr>(function) ||
            ::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalCheckpointOnly>(function) ||
            (bodyNeedsOrderedNBA[index] && !everySiteGenerated) ||
            llvm::any_of(bodyWideRoots[index], [&](uint32_t root) {
              return !orderedRootClosed[root] || !queuePayloadSupported[root];
            });
        if (!runtimeOwner)
          continue;
        ::obelisk::schedule::set<evalRuntimeNBARequiredAttr>(
            function, UnitAttr::get(module.getContext()));
        if (bodyNeedsOrderedNBA[index] && everySiteGenerated) {
          everySiteGenerated = false;
          addedRuntimeOwner = true;
        }
        for (uint32_t root : bodyWideRoots[index])
          if (orderedRootClosed[root]) {
            orderedRootClosed[root] = 0;
            addedRuntimeOwner = true;
          }
      }
    } while (addedRuntimeOwner);
    for (auto [index, function] : llvm::enumerate(admittedBodies))
      if (bodyNeedsOrderedNBA[index] &&
          !::obelisk::schedule::has<evalRuntimeNBARequiredAttr>(function))
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalOrderedNbaQueue>(
            function, UnitAttr::get(module.getContext()));
    if (failed(materializeGeneratedNBAAccumulators(module, staticNBAPlan)))
      return failure();
    directStaticState |=
        llvm::any_of(staticNBAPlan.generatedAccumulators,
                     [](const std::string &name) { return !name.empty(); });
    for (auto [root, accumulator] : llvm::zip_equal(
             staticNBAPlan.roots, staticNBAPlan.generatedAccumulators))
      if (!accumulator.empty())
        stateLayout->directHandles.insert(root.static_state);
  }
  if (useAOT) {
    for (auto &entry : analyses) {
      auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
      if (!function)
        return failure();
      auto bytecode = aotEligibility.getBytecodeFragments().find(entry.first);
      if (bytecode == aotEligibility.getBytecodeFragments().end())
        continue;
      SmallPtrSet<Block *, 8> bytecodeBlocks(bytecode->second.begin(),
                                             bytecode->second.end());
      auto activationRequiresBytecode = [&](Block *start) {
        SmallVector<Block *> pending{start};
        SmallPtrSet<Block *, 16> visited;
        while (!pending.empty()) {
          Block *block = pending.pop_back_val();
          if (!visited.insert(block).second)
            continue;
          if (bytecodeBlocks.contains(block))
            return true;
          Operation *terminator = block->getTerminator();
          if (sim::isSuspensionOp(terminator))
            continue;
          llvm::append_range(pending, terminator->getSuccessors());
        }
        return false;
      };
      SmallVector<uint32_t> &continuations =
          aotBytecodeContinuations[entry.first];
      if (activationRequiresBytecode(&function.getBody().front()))
        continuations.push_back(0);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        if (activationRequiresBytecode(suspension.continuation)) {
          continuations.push_back(suspension.continuationID);
        }
      llvm::sort(continuations);
      continuations.erase(
          std::unique(continuations.begin(), continuations.end()),
          continuations.end());
    }
  }
  // A partial Auto island needs a generated executor for every fanout entry.
  // A continuation that reaches a bytecode block has none unless it is a
  // runtime checkpoint. Detect that here: once packed lowering has emitted
  // static NBA staging, the late owner check can no longer fall back.
  bool bytecodeFanoutOwner = false;
  if (useAOT && nativeScheduler == schedule::NativeSchedulerMode::Auto &&
      !aotEligibility.isFullyEligible() && staticFanoutPlan.exact) {
    llvm::DenseMap<uint32_t, sim::SimFuncOp> actorsBySlot;
    if (metadataDesign)
      metadataDesign.walk([&](sim::SimFuncOp actor) {
        if (std::optional<uint32_t> slot = aotActorSlotFor(actor))
          actorsBySlot.try_emplace(*slot, actor);
      });
    // The coordinator drains a finite initial loop that carries repeat state
    // before entering the periodic loop, so such a bootstrap needs no
    // executor when a periodic clock exists (see isFiniteInitialBootstrap).
    std::optional<bool> hasPeriodicClock;
    auto finiteInitialBootstrap = [&](sim::SimFuncOp actor,
                                      uint32_t continuation) -> FailureOr<bool> {
      if (actor.getEntryKind() != sim::EntryKind::Initial)
        return false;
      bool loopCarried = false;
      actor.walk([&](Operation *operation) {
        schedule::ContinuationSiteAttr site;
        if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
          site = suspend.getSiteAttr();
        if (site && site.getId() == continuation &&
            operation->getNumSuccessors() == 1)
          loopCarried |= operation->getSuccessor(0)->getNumArguments() != 0;
      });
      if (!loopCarried)
        return false;
      if (!hasPeriodicClock) {
        FailureOr<SmallVector<NativePeriodicClock>> clocks =
            buildNativePeriodicClockPlan(module, *stateLayout,
                                         aotEligibility.getActorSlots());
        if (failed(clocks))
          return failure();
        hasPeriodicClock = !clocks->empty();
      }
      return *hasPeriodicClock;
    };
    for (const obelisk_rt_static_fanout_entry &entry :
         staticFanoutPlan.entries) {
      sim::SimFuncOp actor = actorsBySlot.lookup(entry.actor_slot);
      if (!actor)
        continue;
      FailureOr<bool> bootstrap =
          finiteInitialBootstrap(actor, entry.continuation);
      if (failed(bootstrap))
        return failure();
      if (*bootstrap)
        continue;
      auto bytecode = aotBytecodeContinuations.find(actor.getOperation());
      IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
      if (bytecode == aotBytecodeContinuations.end() ||
          !llvm::is_contained(bytecode->second, entry.continuation) ||
          (codeUnit && runtimeCheckpointContinuations.contains(
                           {codeUnit.getUInt(), entry.continuation})))
        continue;
      bytecodeFanoutOwner = true;
      if (detailedTiming)
        llvm::errs() << "partial eval disabled: bytecode fanout owner actor="
                     << entry.actor_slot
                     << " continuation=" << entry.continuation << '\n';
      break;
    }
  }
  // The packed NBA lowering below emits references to generated schedule
  // globals. Decide a partial Auto fallback before that irreversible rewrite.
  if (nativeScheduler == schedule::NativeSchedulerMode::Auto &&
      !aotEligibility.isFullyEligible() &&
      (!staticFanoutPlan.exact || bytecodeFanoutOwner)) {
    useAOT = false;
    // The exact transition set belongs to the discarded eval fanout plan.
    // Generic scheduling must check direct-state writes for transitions again.
    stateLayout->transitionHandlesExact = false;
    stateLayout->transitionHandles.clear();
    staticControl = false;
    staticFanout = false;
    staticNBA = false;
    cleanSuperstep = false;
    staticEvalIsland = false;
    aotBytecodeContinuations.clear();
  }
  if (useAOT) {
    FailureOr<SmallVector<NativePeriodicClock>> clocks =
        buildNativePeriodicClockPlan(module, *stateLayout,
                                     aotEligibility.getActorSlots());
    if (failed(clocks))
      return failure();
    periodicClocks = std::move(*clocks);
    FailureOr<SmallVector<NativePeriodicAlias>> aliases =
        buildNativePeriodicAliasPlan(module, *stateLayout,
                                     aotEligibility.getActorSlots(),
                                     periodicClocks);
    if (failed(aliases))
      return failure();
    periodicAliases = std::move(*aliases);
    if (failed(materializeNativePeriodicClockPlan(module, periodicClocks)))
      return failure();
  }
  // Auto selects the generated eval form after the closed-world slot and
  // fanout proofs exist. A periodic clock enables run-until compression;
  // clockless designs retain calendar ownership in the trusted AOT node loop
  // and still use generated event-driven coordinators.
  if (nativeScheduler == schedule::NativeSchedulerMode::Auto)
    evalScheduler = cleanSuperstep && staticFanoutPlan.exact;
  if (staticSpecialization && useAOT) {
    FailureOr<SmallVector<obelisk_rt_static_actor_root>> dependencies =
        buildNativeStaticActorRootPlan(module, *stateLayout,
                                       aotEligibility.getActorSlots(),
                                       checkpointOnlyActors);
    if (failed(dependencies))
      return failure();
    staticActorRoots = std::move(*dependencies);
  }
  if (useAOT) {
    FailureOr<NativeThreeTierPlan> planned =
        buildNativeThreeTierPlan(module, *stateLayout);
    if (failed(planned))
      return failure();
    threeTierPlan = std::move(*planned);
  }
  markTiming("AOT and static schedule planning");
  FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
      analysis::SimulationScheduleAnalysis::compute(module);
  if (failed(scheduleRanks))
    return failure();
  // Certify before packed lowering turns scalar storage accesses into runtime
  // ABI calls and pointers. Those implementation details are not managed heap
  // use. Generated bodies added later conservatively retain a managed scope.
  module.walk([&](sim::SimFuncOp function) {
    function->removeAttr("obelisk.native.unmanaged");
    if (detail::isUnmanagedNativeProcess(function))
      function->setAttr("obelisk.native.unmanaged", UnitAttr::get(context));
  });
  // Root records are native implementation details, not canonical process
  // state. Insert them only after suspension-live semantic values have been
  // threaded and the shared native/bytecode frame has been analyzed. LLVM
  // coroutine lowering preserves these fixed entry allocas across resume.
  if (failed(instrumentManagedRoots(module)))
    return failure();
  bool guardedAOTSpecialization =
      staticSpecialization && useAOT && aotEligibility.isFullyEligible() &&
      vpi.allowsWrite() && (directStaticState || staticNBA);
  // Writable VPI can invalidate specialization between activations. Keep the
  // original coroutine bodies guarded: they are also the transactional
  // fallback bodies, so marking them permanently clean would suppress the
  // transition publications needed after an external deposit.
  if (failed(markCleanStaticNBAsInGuardedBodies(
          module, guardedAOTSpecialization, staticNBAPlan.siteRoots,
          staticNBAPlan.roots, *stateLayout)))
    return failure();

  // Continuous variable assignments need retained values for force/release;
  // until clean lowering records those planes, keep their canonical route.
  bool hasContinuousStore = false;
  module.walk([&](sim::SimRefStoreOp store) {
    auto kind = store->getParentOfType<sim::SimFuncOp>().getEntryKind();
    hasContinuousStore |= store->hasAttr("obelisk_sim.continuous_store") ||
                          kind == sim::EntryKind::Continuous ||
                          kind == sim::EntryKind::PortInput ||
                          kind == sim::EntryKind::PortOutput;
  });
  bool cleanWritableEval = evalScheduler && vpi.allowsWrite() &&
                           !hasLanguageOverride && !hasContinuousStore;
  stateLayout->directContinuous =
      directStaticState && !useAOT && vpi.allowsWrite() && hasContinuousStore;
  if (cleanWritableEval)
    materializeCleanEvalBodies(metadataDesign);
  auto evalStateLayout = cleanWritableEval
                             ? detail::makeCleanEvalStateLayout(*stateLayout)
                             : *stateLayout;
  if (failed(materializeEvalTwoStateVariants(module, metadataDesign,
                                             evalStateLayout, evalScheduler,
                                             aotActorSlotsByCodeUnit)))
    return failure();
  markTiming("schedule ranks, roots, and two-state variants");

  // Direct fragment extraction can end immediately before an observer
  // suspension, leaving its binding token unused in the generated eval body.
  // Observer tokens intentionally lower only together with a suspension; do
  // not ask dialect conversion to manufacture an invalid integer-typed
  // observer.bind for these dead fragments.
  SmallVector<sim::SimObserverBindOp> deadObserverBindings;
  module.walk([&](sim::SimObserverBindOp binding) {
    if (binding.getResult().use_empty())
      deadObserverBindings.push_back(binding);
  });
  for (sim::SimObserverBindOp binding : deadObserverBindings)
    binding.erase();

  // Packed lowering may replace generated region functions and intentionally
  // drops planning-only attributes. Snapshot typed body-fusion identities
  // while their current actor sites and stable source code units coexist.
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> preLowerFusionOwners;
  DenseMap<uint64_t, uint32_t> preLowerFusionSourceCodeUnits;
  DenseSet<uint64_t> preLowerGeneratedRegionCodeUnits;
  bool invalidPreLowerFusion = false;
  SmallVector<sim::SimFuncOp> currentActors;
  if (metadataDesign)
    metadataDesign.walk([&](sim::SimFuncOp actor) {
      if (aotActorSlotFor(actor))
        currentActors.push_back(actor);
    });
  for (sim::SimFuncOp actor : currentActors) {
    uint32_t actorSlot = *aotActorSlotFor(actor);
    if (::obelisk::schedule::has<schedule::metadata::nativeRegionBody>(actor) &&
        ::obelisk::schedule::has<
            schedule::metadata::evalReconstructsContinuationArgs>(actor)) {
      IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
      if (!codeUnit)
        return actor.emitOpError(
                   "generated native region has no stable code-unit ID"),
               failure();
      preLowerGeneratedRegionCodeUnits.insert(codeUnit.getUInt());
    }
    auto group =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalFusionGroup>(
            actor);
    if (!group)
      continue;
    if (group.getInt() < 0 ||
        static_cast<uint64_t>(group.getInt()) > UINT32_MAX)
      return actor.emitOpError("has an invalid eval fusion group"), failure();
    uint32_t groupID = static_cast<uint32_t>(group.getUInt());
    if (ArrayAttr owners = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::EvalSourceOwners>(actor))
      for (Attribute attribute : owners) {
        auto owner = dyn_cast<schedule::SourceOwnerAttr>(attribute);
        auto codeUnit = owner ? owner.getCodeUnit() : IntegerAttr{};
        if (!codeUnit || codeUnit.getInt() < 0)
          return actor.emitOpError("has a malformed eval source owner"),
                 failure();
        auto [entry, inserted] = preLowerFusionSourceCodeUnits.try_emplace(
            codeUnit.getUInt(), groupID);
        if (!inserted && entry->second != groupID) {
          actor.emitError("source code unit appears in multiple fusion groups");
          invalidPreLowerFusion = true;
        }
      }
    actor.walk([&](Operation *operation) {
      schedule::ContinuationSiteAttr site;
      if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
        site = suspend.getSiteAttr();
      if (!site || site.getId() == 0)
        return;
      auto [entry, inserted] = preLowerFusionOwners.try_emplace(
          std::pair{actorSlot, site.getId()}, groupID);
      if (!inserted && entry->second != groupID) {
        actor.emitError("continuation appears in multiple fusion groups");
        invalidPreLowerFusion = true;
      }
    });
  }
  if (invalidPreLowerFusion)
    return failure();

  // Preserve explicitly selected-body NBA facts before packed conversion can
  // replace their containing function. Generated variants receive the same
  // operation-local certificate when they are created above.
  annotateCompactNBAMetadata(module);

  bool enableDirectStaticState = directStaticState;
  if (failed(lowerPackedSimulationOperations(
          module, dataLayout, *stateLayout, enableDirectStaticState,
          staticNBA ? &staticNBAPlan : nullptr, vpi.allowsWrite(),
          /*experimentalTwoState=*/false)))
    return failure();
  markTiming("packed simulation lowering");

  FailureOr<SmallVector<NativeDirectFragment>> directFragments =
      materializeDirectFragments(
          module, metadataDesign, aotActorSlotsByCodeUnit, analyses,
          aotBytecodeContinuations, preLowerGeneratedRegionCodeUnits,
          runtimeCheckpointContinuations,
          useAOT && cleanSuperstep && staticFanoutPlan.exact &&
              (cleanWritableEval || !guardedAOTSpecialization));
  if (failed(directFragments))
    return failure();
  markTiming("direct fragment materialization");

  // All direct bodies and wrappers exist now. Ownership selection only changes
  // their metadata, so share indexes throughout the selected call closures.
  SymbolTableCollection evalSymbols;

  // Resolve typed graph-fusion membership before eval ownership.  Fusion may
  // replace several source actor continuations with one outlined
  // module-instance body, so the source-owner set must be expanded while the
  // current compute graph and its fusion certificate are both available.
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> aotFusionGroups =
      std::move(preLowerFusionOwners);
  DenseMap<uint64_t, uint32_t> fusionGroupsBySourceCodeUnit =
      std::move(preLowerFusionSourceCodeUnits);
  DenseMap<uint32_t, uint32_t> fragmentFusionGroups;
  if (useAOT) {
    ArrayAttr fusions =
        ::obelisk::schedule::get<schedule::metadata::staticFusion>(
            metadataDesign);
    schedule::ComputeGraphAttr graph = metadataDesign.getComputeGraphAttr();
    if (fusions && graph) {
      for (Attribute fusionAttribute : fusions) {
        auto fusion = dyn_cast<schedule::ComputeFusionAttr>(fusionAttribute);
        if (!fusion)
          return metadataDesign.emitOpError(
                     "has malformed static fusion metadata"),
                 failure();
        for (int64_t fragmentIndex : fusion.getFragments().asArrayRef()) {
          if (fragmentIndex < 0 ||
              static_cast<uint64_t>(fragmentIndex) >= graph.getNodes().size())
            return metadataDesign.emitOpError(
                       "static fusion references an invalid compute fragment"),
                   failure();
          auto [entry, inserted] = fragmentFusionGroups.try_emplace(
              static_cast<uint32_t>(fragmentIndex), fusion.getId());
          if (!inserted && entry->second != fusion.getId())
            return metadataDesign.emitOpError(
                       "compute fragment appears in multiple fusion groups"),
                   failure();
        }
      }
    }
  }
  // Static fanout has already resolved the final physical actor/site pair
  // before packed lowering rewrites CFGs. Join that stable plan to fusion by
  // current-generation graph fragment, avoiding all post-rewrite block
  // ordinal lookups.
  for (const auto &[sourceOwner, fragments] : staticFanoutPlan.fragments) {
    uint32_t sourceGroup = UINT32_MAX;
    // A partially covered owner shares one fragment with a fused body while
    // keeping the rest outside it. The group belongs to that fragment, not to
    // the owner, and claiming it would contradict the owner's body
    // certificate.
    bool coversEveryFragment = true;
    for (uint32_t fragment : fragments) {
      auto group = fragmentFusionGroups.find(fragment);
      if (group == fragmentFusionGroups.end()) {
        coversEveryFragment = false;
        continue;
      }
      if (sourceGroup != UINT32_MAX && sourceGroup != group->second)
        return module.emitError(
            "one physical fanout owner crosses multiple fusion groups");
      sourceGroup = group->second;
    }
    if (sourceGroup != UINT32_MAX && coversEveryFragment) {
      auto [entry, inserted] =
          aotFusionGroups.try_emplace(sourceOwner, sourceGroup);
      if (!inserted && entry->second != sourceGroup)
        return module.emitError(
            "physical fanout owner disagrees with its fusion certificate");
    }
  }
  auto fusionGroupFor = [&](uint32_t slot, uint32_t continuation) {
    auto found = aotFusionGroups.find({slot, continuation});
    return found == aotFusionGroups.end() ? UINT32_MAX : found->second;
  };
  // Expand a fused executor through typed physical owners, then attach only
  // current-generation graph fragments for ownership and SCC analysis.
  for (NativeDirectFragment &direct : *directFragments) {
    if (direct.fusionGroup == UINT32_MAX)
      direct.fusionGroup =
          fusionGroupFor(direct.actorSlot, direct.continuation);
    if (direct.fusionGroup == UINT32_MAX)
      for (uint64_t codeUnit : direct.sourceCodeUnits)
        if (auto group = fusionGroupsBySourceCodeUnit.find(codeUnit);
            group != fusionGroupsBySourceCodeUnit.end()) {
          if (direct.fusionGroup != UINT32_MAX &&
              direct.fusionGroup != group->second)
            return module.emitError(
                "direct eval body crosses multiple fusion groups");
          direct.fusionGroup = group->second;
        }
    llvm::sort(direct.sourceOwners);
    direct.sourceOwners.erase(
        std::unique(direct.sourceOwners.begin(), direct.sourceOwners.end()),
        direct.sourceOwners.end());
    if (direct.fusionGroup == UINT32_MAX)
      for (auto sourceOwner : direct.sourceOwners)
        if (auto group = aotFusionGroups.find(sourceOwner);
            group != aotFusionGroups.end()) {
          if (direct.fusionGroup != UINT32_MAX &&
              direct.fusionGroup != group->second)
            return module.emitError(
                "direct eval body crosses multiple fusion groups");
          direct.fusionGroup = group->second;
        }
    // Only the outlined instance coordinator executes the complete fusion
    // group. Helpers referenced by that coordinator may retain the same group
    // provenance, but expanding each helper to all physical source owners
    // would make several distinct bodies claim every fused fragment.
    if (direct.instanceCoordinator && direct.fusionGroup != UINT32_MAX)
      for (const auto &[sourceOwner, group] : aotFusionGroups)
        if (group == direct.fusionGroup)
          direct.sourceOwners.push_back(sourceOwner);
    llvm::sort(direct.sourceOwners);
    direct.sourceOwners.erase(
        std::unique(direct.sourceOwners.begin(), direct.sourceOwners.end()),
        direct.sourceOwners.end());
    for (auto sourceOwner : direct.sourceOwners) {
      // Stable source-owner metadata on an ordinary generated body records
      // provenance, not whole-group execution. It may recover coverage for a
      // source activation erased by fusion, but a preserved exact body keeps
      // ownership of its own physical fragment. An explicit instance
      // coordinator is different: it replaces every certified source
      // activation in the group and therefore retains complete coverage.
      bool preservedExactBody =
          !direct.instanceCoordinator &&
          llvm::any_of(*directFragments, [&](const auto &body) {
            return &body != &direct && !body.instanceCoordinator &&
                   body.actorSlot == sourceOwner.first &&
                   body.continuation == sourceOwner.second;
          });
      if (preservedExactBody)
        continue;
      if (auto fragments = staticFanoutPlan.fragments.find(sourceOwner);
          fragments != staticFanoutPlan.fragments.end())
        llvm::append_range(direct.fragmentIDs, fragments->second);
    }
    llvm::sort(direct.fragmentIDs);
    direct.fragmentIDs.erase(
        std::unique(direct.fragmentIDs.begin(), direct.fragmentIDs.end()),
        direct.fragmentIDs.end());
  }

  NativeEvalOwnershipPlan evalOwnership;
  if (evalScheduler) {
    FailureOr<NativeEvalOwnershipPlan> ownership =
        buildNativeEvalOwnershipPlan(module, *stateLayout, staticFanoutPlan,
                                     *directFragments, periodicAliases);
    if (failed(ownership))
      return failure();
    evalOwnership = std::move(*ownership);
    // Exact unfused actor/continuation ownership is resolved against the
    // current static-fanout plan. Transfer that plan's current-generation
    // fragment coverage to the direct body so downstream closure/SCC
    // analysis sees the same physical nodes. This replaces the old unsafe
    // FragmentABI ordinal fallback.
    for (auto [entryIndex, entry] : llvm::enumerate(staticFanoutPlan.entries)) {
      if (entryIndex >= evalOwnership.fanoutOwners.size())
        return module.emitError("eval ownership plan is incomplete"), failure();
      const NativeEvalFanoutOwner &owner =
          evalOwnership.fanoutOwners[entryIndex];
      if (owner.kind != NativeEvalFanoutOwnerKind::Direct ||
          owner.directFragment >= directFragments->size())
        continue;
      auto coverage = staticFanoutPlan.fragments.find(
          {entry.actor_slot, entry.continuation});
      if (coverage == staticFanoutPlan.fragments.end())
        continue;
      llvm::append_range((*directFragments)[owner.directFragment].fragmentIDs,
                         coverage->second);
    }
    for (NativeDirectFragment &direct : *directFragments) {
      llvm::sort(direct.fragmentIDs);
      direct.fragmentIDs.erase(
          std::unique(direct.fragmentIDs.begin(), direct.fragmentIDs.end()),
          direct.fragmentIDs.end());
    }
  }

  // A declined owner keeps its four-state body, so the runtime call stays
  // inline. Admitting it to the generated closure would trip the closure
  // verifier, or reduce the activation to a bare checkpoint publication that
  // drops the body's NBA staging.
  if (evalScheduler) {
    std::string unsupportedCheckpointOwner;
    for (const NativeDirectFragment &direct : *directFragments) {
      auto wrapper =
          evalSymbols.getSymbolTable(module).lookup<LLVM::LLVMFuncOp>(
              direct.wrapper);
      if (!wrapper)
        continue;
      auto owner = ::obelisk::schedule::get<
          schedule::metadata::evalUnsupportedCheckpointOwner>(wrapper);
      if (!owner)
        continue;
      unsupportedCheckpointOwner =
          "an eval owner keeps an unguarded runtime leaf in " +
          owner.getValue().str();
      break;
    }
    if (!unsupportedCheckpointOwner.empty()) {
      if (nativeScheduler != schedule::NativeSchedulerMode::Auto)
        return module.emitError(unsupportedCheckpointOwner), failure();
      module.emitRemark("generated eval disabled: ")
          << unsupportedCheckpointOwner;
      if (detailedTiming)
        llvm::errs() << "generated eval disabled: "
                     << unsupportedCheckpointOwner << '\n';
      evalScheduler = false;
    }
  }

  // A direct body may claim a Tier-2 SCC only after every typed source owner
  // has been resolved to current-graph coverage. Until SCC-only functions are
  // outlined, every direct executor that intersects a convergence group runs
  // in the generated coordinator's global dirty-mask fixpoint. Clearing each
  // such owner's bit before execution preserves self- and cross-owner
  // republication; the union of direct owners must still cover the SCC.
  if (evalScheduler) {
    std::string invalidConvergenceOwnership;

    // Procedural event waits are not graph-level settling SCCs: projecting
    // every sensitivity edge through its resume edge would make every
    // repeating clocked process look cyclic. A direct executor nevertheless
    // has to retain a publication that reactivates an earlier wait in that
    // same executor. Detect that local feedback by following only the
    // process-order/resume path from the watched suspension back to the
    // publishing fragment. The generated coordinator will then consume the
    // old ready bit before execution, allowing the new occurrence to remain
    // queued for the next activation.
    if (schedule::ComputeGraphAttr graph =
            metadataDesign.getComputeGraphAttr()) {
      SmallVector<SmallVector<uint32_t>> proceduralSuccessors(
          graph.getNodes().size());
      SmallVector<schedule::ComputeEdgeAttr> sensitivityEdges;
      for (Attribute attribute : graph.getEdges()) {
        auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
        if (edge.getKind() == schedule::ComputeEdgeKind::ProcessOrder ||
            edge.getKind() == schedule::ComputeEdgeKind::Resume)
          proceduralSuccessors[edge.getSource()].push_back(edge.getTarget());
        else if (edge.getKind() == schedule::ComputeEdgeKind::Sensitivity)
          sensitivityEdges.push_back(edge);
      }
      for (NativeDirectFragment &direct : *directFragments) {
        llvm::SmallDenseSet<uint32_t, 16> members(direct.fragmentIDs.begin(),
                                                  direct.fragmentIDs.end());
        for (schedule::ComputeEdgeAttr sensitivity : sensitivityEdges) {
          if (!members.contains(sensitivity.getSource()) ||
              !members.contains(sensitivity.getTarget()))
            continue;
          SmallVector<uint32_t> pending{sensitivity.getTarget()};
          llvm::SmallDenseSet<uint32_t, 16> visited;
          while (!pending.empty()) {
            uint32_t fragment = pending.pop_back_val();
            if (!visited.insert(fragment).second)
              continue;
            if (fragment == sensitivity.getSource()) {
              direct.tier2Convergence = true;
              break;
            }
            for (uint32_t successor : proceduralSuccessors[fragment])
              if (members.contains(successor))
                pending.push_back(successor);
          }
          if (direct.tier2Convergence)
            break;
        }
      }
    }

    for (const NativeThreeTierKernelPlan &kernel : threeTierPlan.kernels) {
      if (kernel.tier != schedule::SchedulerTierKind::Tier2 ||
          kernel.schedule != schedule::ComputeScheduleKind::Convergence)
        continue;
      bool hasDirectOwner = false;
      for (NativeDirectFragment &direct : *directFragments) {
        auto ownsFragment = [&](uint32_t fragment) {
          return llvm::is_contained(direct.fragmentIDs, fragment);
        };
        if (llvm::any_of(kernel.memberIDs, ownsFragment)) {
          direct.tier2Convergence = true;
          hasDirectOwner = true;
        }
      }
      if (!hasDirectOwner) {
        std::string memberSummary;
        for (uint32_t member : kernel.memberIDs) {
          if (!memberSummary.empty())
            memberSummary += ",";
          memberSummary += std::to_string(member);
          if (member < threeTierPlan.sourceGraph.getNodes().size())
            if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(
                    threeTierPlan.sourceGraph.getNodes()[member]))
              memberSummary +=
                  (Twine("@") + fragment.getFunction().getValue()).str();
        }
        std::string directSummary;
        for (const NativeDirectFragment &direct : *directFragments) {
          if (!directSummary.empty())
            directSummary += ",";
          directSummary +=
              (Twine(direct.actorSlot) + "/" + Twine(direct.continuation) + "[")
                  .str();
          for (uint32_t fragment : direct.fragmentIDs) {
            if (directSummary.back() != '[')
              directSummary += ",";
            directSummary += std::to_string(fragment);
          }
          directSummary += "]";
        }
        invalidConvergenceOwnership =
            (Twine("a Tier-2 SCC has no direct eval owner (kernel=") +
             Twine(kernel.id) + ", owner=" + Twine(kernel.owner) +
             ", members=" + memberSummary + ", direct=" + directSummary +
             ", clean=" + Twine(cleanSuperstep) + ", fanout=" +
             Twine(staticFanout) + ", island=" + Twine(staticEvalIsland) + ")")
                .str();
        break;
      }
    }
    if (!invalidConvergenceOwnership.empty()) {
      if (nativeScheduler != schedule::NativeSchedulerMode::Auto)
        return module.emitError(invalidConvergenceOwnership), failure();
      module.emitRemark("generated eval disabled: ")
          << invalidConvergenceOwnership;
      if (detailedTiming)
        llvm::errs() << "generated eval disabled: "
                     << invalidConvergenceOwnership << '\n';
      evalScheduler = false;
      for (NativeDirectFragment &direct : *directFragments)
        direct.tier2Convergence = false;
    }
  }

  if (evalScheduler) {
    SmallVector<sim::SimFuncOp> actorsBySlot(
        aotEligibility.getActorSlots().size());
    metadataDesign.walk([&](sim::SimFuncOp actor) {
      std::optional<uint32_t> slot = aotActorSlotFor(actor);
      if (slot && *slot < actorsBySlot.size())
        actorsBySlot[*slot] = actor;
    });
    auto isFiniteInitialBootstrap = [&](uint32_t actorSlot,
                                        uint32_t continuation) {
      if (actorSlot >= actorsBySlot.size())
        return false;
      sim::SimFuncOp actor = actorsBySlot[actorSlot];
      if (!actor || actor.getEntryKind() != sim::EntryKind::Initial)
        return false;
      bool loopCarriedContinuation = false;
      actor.walk([&](Operation *operation) {
        schedule::ContinuationSiteAttr site;
        if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
          site = suspend.getSiteAttr();
        if (site && site.getId() == continuation &&
            operation->getNumSuccessors() == 1)
          loopCarriedContinuation |=
              operation->getSuccessor(0)->getNumArguments() != 0;
      });
      return loopCarriedContinuation;
    };
    llvm::SmallDenseSet<StringRef, 16> executors;
    for (auto [entryIndex, entry] : llvm::enumerate(staticFanoutPlan.entries)) {
      // A finite initial loop is deliberately runtime-owned while it carries
      // repeat/control state. Periodic handoff drains this bootstrap prefix
      // and checks that no such subscription remains live before entering the
      // generated loop, so it is not a member of the steady-state owner set.
      if (!periodicClocks.empty() &&
          isFiniteInitialBootstrap(entry.actor_slot, entry.continuation))
        continue;
      if (entryIndex >= evalOwnership.fanoutOwners.size())
        return module.emitError("eval ownership plan is incomplete"), failure();
      const NativeEvalFanoutOwner &owner =
          evalOwnership.fanoutOwners[entryIndex];
      if (owner.kind == NativeEvalFanoutOwnerKind::PeriodicAlias)
        continue;
      // IEEE 1800-2023 4.5: Reactive actors deliberately remain runtime
      // owners. Their subscriptions arbitrate against Re-Inactive/Re-NBA;
      // they are not missing executors in the generated Active closure.
      if (owner.kind == NativeEvalFanoutOwnerKind::Runtime &&
          entry.actor_slot < actorsBySlot.size() &&
          actorsBySlot[entry.actor_slot] &&
          actorsBySlot[entry.actor_slot].getHomeRegion() ==
              sim::EventRegion::Reactive)
        continue;
      const NativeDirectFragment *direct =
          owner.kind == NativeEvalFanoutOwnerKind::Direct &&
                  owner.directFragment < directFragments->size()
              ? &(*directFragments)[owner.directFragment]
              : nullptr;
      if (!direct || direct->wrapper.empty()) {
        bool certifiedRuntimeNBAFallback = false;
        if (entry.actor_slot < actorsBySlot.size())
          if (sim::SimFuncOp actor = actorsBySlot[entry.actor_slot])
            if (auto body = ::obelisk::schedule::get<
                    ::obelisk::schedule::Field::EvalBody>(actor))
              if (sim::SimFuncOp evalBody =
                      evalSymbols.getSymbolTable(metadataDesign)
                          .lookup<sim::SimFuncOp>(body.getValue()))
                certifiedRuntimeNBAFallback =
                    ::obelisk::schedule::has<evalRuntimeNBARequiredAttr>(
                        evalBody);
        std::string detail;
        llvm::raw_string_ostream diagnostic(detail);
        diagnostic << "actor=" << entry.actor_slot
                   << " continuation=" << entry.continuation;
        if (entry.actor_slot < actorsBySlot.size() &&
            actorsBySlot[entry.actor_slot])
          diagnostic << " function="
                     << actorsBySlot[entry.actor_slot].getSymName();
        diagnostic << " planned=";
        if (auto planned = staticFanoutPlan.fragments.find(
                {entry.actor_slot, entry.continuation});
            planned != staticFanoutPlan.fragments.end())
          for (uint32_t fragment : planned->second)
            diagnostic << fragment << ",";
        diagnostic << " candidates=";
        for (const auto &candidate : *directFragments)
          if (candidate.actorSlot == entry.actor_slot) {
            diagnostic << "[" << candidate.continuation
                       << " group=" << candidate.fusionGroup
                       << " wrapper=" << candidate.wrapper << ":";
            for (uint32_t fragment : candidate.fragmentIDs)
              diagnostic << fragment << ",";
            diagnostic << " owners=";
            for (auto [actor, continuation] : candidate.sourceOwners)
              diagnostic << actor << "/" << continuation << ",";
            diagnostic << "]";
          }
        if (nativeScheduler != schedule::NativeSchedulerMode::Auto &&
            !certifiedRuntimeNBAFallback)
          return module.emitError("eval exact owner miss: " + detail),
                 failure();
        if (detailedTiming)
          llvm::errs()
              << (certifiedRuntimeNBAFallback
                      ? "generated eval disabled by runtime-ordered NBA owner: "
                      : "auto eval exact owner miss: ")
              << detail << '\n';
        if (certifiedRuntimeNBAFallback)
          ::obelisk::schedule::set<evalRuntimeNBAFallbackAttr>(
              module, UnitAttr::get(context));
        evalScheduler = false;
        break;
      }
      executors.insert(direct->wrapper);
    }
    if (executors.empty()) {
      if (detailedTiming)
        llvm::errs() << "generated eval coordinator capacity rejected: owners="
                     << executors.size() << '\n';
      evalScheduler = false;
    }
  }

  // Auto's partial island is profitable only when the generated coordinator
  // has an exact executor for every admitted fanout entry. A late owner miss
  // must fall back to the generic scheduler, not the legacy hybrid wrapper:
  // the latter cannot claim a partially admitted actor's framed continuation.
  if (nativeScheduler == schedule::NativeSchedulerMode::Auto &&
      !aotEligibility.isFullyEligible() && !evalScheduler) {
    if (staticNBA) {
      emitError(module.getLoc())
          << "partial eval ownership failed after static NBA lowering";
      return failure();
    }
    useAOT = false;
    stateLayout->transitionHandlesExact = false;
    stateLayout->transitionHandles.clear();
    staticControl = false;
    staticFanout = false;
    staticNBA = false;
    cleanSuperstep = false;
    staticEvalIsland = false;
  }

  if (evalScheduler) {
    for (NativeDirectFragment &direct : *directFragments) {
      if (direct.twoStateWrapper.empty())
        continue;
      // The variant exists only when StateDomainAnalysis proved the complete
      // source-level call closure inductively two-state. Graph ownership and
      // continuation rebuilding decide when this body runs, but cannot
      // invalidate that value-domain proof. The selected body is entered only
      // after the canonical unknown-plane precondition below succeeds.
      sim::SimFuncOp body = evalSymbols.getSymbolTable(metadataDesign)
                                .lookup<sim::SimFuncOp>(direct.twoStateBody);
      if (!body)
        return module.emitError("selected eval variant body is missing");
      SmallVector<sim::SimFuncOp> pending{body};
      SmallVector<sim::SimFuncOp> closure;
      llvm::SmallPtrSet<Operation *, 8> seen;
      while (!pending.empty()) {
        sim::SimFuncOp selected = pending.pop_back_val();
        if (!seen.insert(selected.getOperation()).second)
          continue;
        closure.push_back(selected);
        selected.walk([&](sim::SimCallOp call) {
          sim::SimFuncOp callee = evalSymbols.getSymbolTable(metadataDesign)
                                      .lookup<sim::SimFuncOp>(call.getCallee());
          if (callee &&
              ::obelisk::schedule::has<
                  ::obelisk::schedule::Field::EvalInductiveTwoState>(callee))
            pending.push_back(callee);
        });
      }
      for (sim::SimFuncOp selected : closure)
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalSelectedTwoState>(
            selected, UnitAttr::get(context));
    }
    // Carry the selected-owner proof on the operation that consumes it.
    // Function conversion and NBA conversion are intentionally free to run in
    // either order, so an NBA pattern cannot safely inspect its parent op.
    annotateCompactNBAMetadata(module);
  }
  for (NativeDirectFragment &direct : *directFragments) {
    uint32_t physicalGroup =
        fusionGroupFor(direct.actorSlot, direct.continuation);
    if (direct.fusionGroup == UINT32_MAX)
      direct.fusionGroup = physicalGroup;
    else if (physicalGroup != UINT32_MAX && direct.fusionGroup != physicalGroup)
      return module.emitError(
          "direct eval body disagrees with its typed fusion group");
    if (direct.fusionGroup == UINT32_MAX)
      for (uint32_t fragment : direct.fragmentIDs) {
        auto group = fragmentFusionGroups.find(fragment);
        if (group == fragmentFusionGroups.end())
          continue;
        if (direct.fusionGroup != UINT32_MAX &&
            direct.fusionGroup != group->second)
          return module.emitError(
              "direct eval body crosses multiple fusion groups");
        direct.fusionGroup = group->second;
      }
  }
  markTiming("eval ownership and graph planning");

  // Runtime declarations are shared by all actors. Index the module only after
  // declaring them, then keep the index current as helpers are materialized.
  if (!analyses.empty())
    detail::declareProcessSpawnRuntimeABI(module);
  SymbolTable helperSymbols(module);
  SmallVector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>
      rankedAOTNodes;
  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    NativeSchedulePlan schedule;
    schedule.initialRank = scheduleRanks->getEntryRank(entry.first).value_or(0);
    if (useAOT)
      schedule.actorSlot = aotActorSlotFor(function);
    if (schedule.actorSlot) {
      auto bytecode = aotBytecodeContinuations.find(entry.first);
      if (bytecode != aotBytecodeContinuations.end())
        schedule.bytecodeContinuations = bytecode->second;
    }
    DenseMap<uint32_t, uint32_t> continuationRanks;
    for (const ProcessSuspension &suspension : entry.second->getSuspensions()) {
      uint32_t rank =
          scheduleRanks->getBlockRank(suspension.continuation).value_or(0);
      auto [rankIt, inserted] =
          continuationRanks.try_emplace(suspension.continuationID, rank);
      if (!inserted && rankIt->second != rank)
        return suspension.operation->emitError(
            "continuation ID has inconsistent schedule ranks");
    }
    for (auto [continuation, rank] : continuationRanks)
      schedule.continuations.emplace_back(continuation, rank);
    llvm::sort(schedule.continuations, [](const auto &left, const auto &right) {
      return left.first < right.first;
    });
    if (schedule.actorSlot) {
      // Packed signature conversion may replace the entry block. Its old
      // pointer is not a stable schedule identity: a new block can reuse that
      // address and accidentally inherit another actor's rank. Use the same
      // captured actor-entry rank as the spawn helper.
      rankedAOTNodes.emplace_back(
          schedule.initialRank,
          *schedule.actorSlot, 0, UINT32_MAX);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        rankedAOTNodes.emplace_back(
            scheduleRanks->getBlockRank(suspension.continuation).value_or(0),
            *schedule.actorSlot, suspension.continuationID,
            fusionGroupFor(*schedule.actorSlot, suspension.continuationID));
    }
    if (failed(makeProcessActivationHelper(module, helperSymbols, function,
                                           *entry.second)))
      return failure();
    if (failed(
            makeProcessSpawnHelper(module, helperSymbols, function,
                                   *entry.second, schedule)))
      return failure();
  }
  markTiming("process activation and spawn helpers");
  if (useAOT) {
    // The production eval coordinator folds Tier-2 convergence ownership into
    // its ready-mask fixed point. Do not emit a second, disconnected schedule
    // graph: direct fragments classified above are the subkernels actually
    // called by run_until's coordinator.
    llvm::SmallDenseSet<uint32_t, 16> entrySlots;
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      (void)fusionGroup;
      if (slot >= aotEligibility.getActorSlots().size())
        return module.emitError("AOT node references an invalid actor slot");
      if (continuation == 0)
        entrySlots.insert(slot);
    }
    if (entrySlots.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "AOT node inventory is missing an actor entry continuation");
    llvm::sort(rankedAOTNodes);
    rankedAOTNodes.erase(
        std::unique(rankedAOTNodes.begin(), rankedAOTNodes.end()),
        rankedAOTNodes.end());
    SmallVector<obelisk_rt_native_schedule_node> executableNodes;
    executableNodes.reserve(rankedAOTNodes.size());
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      executableNodes.push_back({slot, continuation, fusionGroup});
    }
    bool rootSlotZero =
        llvm::any_of(aotEligibility.getActorSlots(), [](const auto &entry) {
          auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
          return function &&
                 function.getEntryKind() == sim::EntryKind::RootInitializer &&
                 entry.second == 0;
        });
    if (evalScheduler) {
      FailureOr<bool> evalPlan = makeNativeEvalPlan(
          module, dataLayout, aotEligibility.getActorSlots().size(),
          executableNodes, *stateLayout, staticNBAPlan, staticFanoutPlan,
          staticActorRoots, *directFragments, evalOwnership,
          threeTierPlan.sourceGraph, periodicClocks, periodicAliases,
          directStaticState, staticNBA, staticControl, staticFanout,
          cleanSuperstep, aotEligibility.isFullyEligible(), staticEvalIsland,
          rootSlotZero, vpi);
      if (failed(evalPlan))
        return failure();
      if (!*evalPlan) {
        if (detailedTiming)
          llvm::errs() << "generated eval disabled: LLVM eligibility proof\n";
        // Direct fragments and their four-/two-state route shells are
        // materialized before the final LLVM-level eligibility proof.  A
        // declined proof therefore takes the same certified legacy handoff
        // as a source-level ordered-NBA owner: the ordinary coroutine keeps
        // the runtime NBA call, and unused Eval route shells are ignored.
        ::obelisk::schedule::set<evalRuntimeNBAFallbackAttr>(
            module, UnitAttr::get(context));
        evalScheduler = false;
      }
    }
    if (!evalScheduler &&
        failed(makeNativeAOTPlanLegacy(
            module, dataLayout, aotEligibility.getActorSlots().size(),
            executableNodes, *stateLayout, staticNBAPlan, staticFanoutPlan,
            staticActorRoots, directStaticState, staticNBA, staticControl,
            staticFanout, cleanSuperstep, aotEligibility.isFullyEligible(),
            rootSlotZero, vpi))) {
      return failure();
    }
  }
  markTiming("native schedule plan materialization");
  if (failed(makeSchedulerMain(module, *stateLayout, useAOT, evalScheduler)))
    return failure();
  markTiming("process helpers and scheduler main");

  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      ordinary.push_back(function);
  });
  SmallVector<PreparedOrdinaryNativeFunction> ordinaryFunctions;
  ordinaryFunctions.reserve(ordinary.size());
  for (sim::SimFuncOp function : ordinary) {
    FailureOr<PreparedOrdinaryNativeFunction> prepared =
        prepareOrdinaryFunction(function);
    if (failed(prepared))
      return failure();
    ordinaryFunctions.push_back(std::move(*prepared));
  }
  markTiming("ordinary function preparation");
  if (failed(failableParallelForEach(
          context, ordinaryFunctions,
          [&](PreparedOrdinaryNativeFunction &function) {
            return lowerPreparedOrdinaryFunction(function);
          })))
    return failure();
  markTiming("ordinary function body lowering");
  // Runtime status checks can make an ordinary direct body return i32 while
  // its wrapper was formed against the pre-runtime void signature. Reconcile
  // the explicitly tagged private call after every ordinary signature is
  // final, and propagate the cold-path status through the wrapper.
  SmallVector<func::CallOp> directCalls;
  module.walk([&](func::CallOp call) {
    if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalDirectCall>(
            call))
      directCalls.push_back(call);
  });
  // All ordinary signatures are final. Replacing calls below changes only
  // function bodies, so one lazy symbol index remains valid for the complete
  // reconciliation. Repeated static lookup otherwise scans both symbol
  // tables for every generated four-state/two-state call.
  SymbolTableCollection directCallSymbols;
  for (func::CallOp call : directCalls) {
    auto callee = directCallSymbols.lookupSymbolIn<func::FuncOp>(
        module, call.getCalleeAttr());
    if (!callee && metadataDesign)
      callee = directCallSymbols.lookupSymbolIn<func::FuncOp>(
          metadataDesign, call.getCalleeAttr());
    if (!callee) {
      return call.emitError()
                 << "direct eval body is missing: " << call.getCallee(),
             failure();
    }
    TypeRange results = callee.getFunctionType().getResults();
    if (call.getResultTypes() == results)
      continue;
    if (call.getNumResults() != 0 || results.size() != 1 ||
        results.front() != IntegerType::get(context, 32))
      return call.emitError("direct eval body has an unsupported status ABI"),
             failure();
    LLVM::ReturnOp returnOp;
    unsigned returnCount = 0;
    auto wrapper = call->getParentOfType<LLVM::LLVMFuncOp>();
    if (wrapper)
      wrapper.walk([&](LLVM::ReturnOp candidate) {
        ++returnCount;
        if (returnCount == 1)
          returnOp = candidate;
      });
    if (returnCount != 1 || !returnOp || returnOp.getNumOperands() != 1)
      return call.emitError("direct eval wrapper has an invalid return"),
             failure();
    OpBuilder callBuilder(call);
    auto replacement =
        func::CallOp::create(callBuilder, call.getLoc(), call.getCalleeAttr(),
                             results, call.getOperands());
    replacement->setAttrs(call->getAttrs());
    returnOp->setOperand(0, replacement.getResult(0));
    call.erase();
  }
  if (failed(materializeManagedMethodThunks(module, dataLayout)))
    return failure();
  markTiming("managed method thunks");

  SmallVector<std::pair<sim::SimFuncOp, SimulationProcessFrameAnalysis *>>
      processFunctions;
  processFunctions.reserve(analyses.size());
  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    processFunctions.emplace_back(function, entry.second.get());
  }
  llvm::sort(processFunctions, [](auto left, auto right) {
    return left.first.getSymName() < right.first.getSymName();
  });

  SmallVector<PreparedPlainNativeProcess> plainProcesses;
  SmallVector<PreparedSuspendableProcess> suspendableProcesses;
  for (auto [function, analysis] : processFunctions) {
    if (analysis->getSuspensions().empty()) {
      FailureOr<PreparedPlainNativeProcess> prepared =
          preparePlainNativeProcess(function, *analysis);
      if (failed(prepared))
        return failure();
      plainProcesses.push_back(std::move(*prepared));
      continue;
    }
    FailureOr<PreparedSuspendableProcess> prepared =
        prepareSuspendableProcess(function, *analysis);
    if (failed(prepared))
      return failure();
    suspendableProcesses.push_back(std::move(*prepared));
  }
  markTiming("process body preparation");
  if (failed(failableParallelForEach(
          context, plainProcesses, [&](PreparedPlainNativeProcess &process) {
            return lowerPreparedPlainNativeProcess(process);
          })))
    return failure();
  markTiming("plain process body lowering");
  if (failed(failableParallelForEach(context, suspendableProcesses,
                                     [&](PreparedSuspendableProcess &process) {
                                       return lowerPreparedSuspendableProcess(
                                           process);
                                     })))
    return failure();
  markTiming("suspendable process body lowering");
  // Only embedded execution/bytecode entries are queried during finalization.
  // Their identities are already frozen; later wrappers and frame descriptors
  // introduce different symbols, so all processes can share this snapshot.
  SymbolTable embeddedSymbols(module);
  for (PreparedPlainNativeProcess &process : plainProcesses)
    if (failed(finishPreparedPlainNativeProcess(process, embeddedSymbols)))
      return failure();
  markTiming("plain process finalization");
  for (PreparedSuspendableProcess &process : suspendableProcesses)
    if (failed(finishPreparedSuspendableProcess(process, embeddedSymbols)))
      return failure();
  markTiming("suspendable process finalization");

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  for (sim::SimDesignOp design : designs) {
    SmallVector<Operation *> nested;
    for (Operation &operation : design.getBody().front())
      nested.push_back(&operation);
    for (Operation *operation : nested) {
      if (isa<sim::SimVPIDefinitionDeclOp, sim::SimVPIDefinitionMemberDeclOp,
              sim::SimVPIDefinitionSpecializationDeclOp,
              sim::SimVPIDefinitionMemberSpecializationOp,
              sim::SimVPIDefinitionMemberInstanceBindingOp,
              sim::SimVPIDefinitionMemberInstanceRelationOp,
              sim::SimScopeDeclOp, sim::SimCodeUnitDeclOp,
              sim::SimStatementDeclOp, sim::SimStatementSiteDeclOp,
              sim::SimVPIStatementRelationDeclOp, sim::SimVPIRelationDeclOp,
              sim::SimVPINetIdentityDeclOp, sim::SimStorageDeclOp,
              sim::SimNetDeclOp, sim::SimDriverDeclOp, sim::SimPortDeclOp,
              sim::SimNetConnectDeclOp, sim::SimPassSwitchDeclOp,
              sim::SimClassDeclOp, sim::SimCovergroupDeclOp,
              sim::SimVPIObjectAnchorOp, sim::SimVPINettypeDeclOp,
              sim::SimVPITypespecDeclOp, sim::SimVPIEnumConstDeclOp,
              sim::SimClassFieldDeclOp, sim::SimClassMethodDeclOp,
              sim::SimRandomConstraintTemplateOp>(operation)) {
        operation->erase();
        continue;
      }
      operation->moveBefore(design);
    }
    design.erase();
  }
  return success();
}

LogicalResult materializeEvalFunctionRoutes(ModuleOp module) {
  // Variants are prepared before the final coordinator eligibility proof.
  // A declined plan has no generated callers or same-slot resume target.
  if (!::obelisk::schedule::has<::obelisk::schedule::Field::EvalGenerated>(
          module)) {
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalPathProbeRoutes>(module);
    ::obelisk::schedule::remove<schedule::metadata::evalCheckpointRoutes>(
        module);
    return success();
  }
  // Input bodies and plan globals are stable during route materialization.
  // Index them once; newly generated route helpers are held by direct handles.
  SymbolTable inputSymbols(module);
  llvm::StringMap<std::string> pathKnownProbes;
  llvm::StringMap<std::string> checkpointPathProbes;
  llvm::StringMap<std::pair<uint32_t, uint32_t>> checkpointOwners;
  if (ArrayAttr mappings = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalPathProbeRoutes>(module)) {
    for (Attribute mappingAttr : mappings) {
      auto mapping = dyn_cast<schedule::PathProbeRouteAttr>(mappingAttr);
      auto twoState = mapping ? mapping.getTwoState() : FlatSymbolRefAttr{};
      auto probe = mapping ? mapping.getProbe() : FlatSymbolRefAttr{};
      auto checkpointProbe =
          mapping ? mapping.getCheckpointProbe() : FlatSymbolRefAttr{};
      if (!twoState || !probe || !checkpointProbe)
        return module.emitError("has malformed eval path-probe route"),
               failure();
      pathKnownProbes[twoState.getValue()] = probe.getValue().str();
      checkpointPathProbes[twoState.getValue()] =
          checkpointProbe.getValue().str();
    }
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalPathProbeRoutes>(module);
  }
  if (ArrayAttr mappings =
          ::obelisk::schedule::get<schedule::metadata::evalCheckpointRoutes>(
              module)) {
    for (Attribute mappingAttr : mappings) {
      auto mapping = dyn_cast<schedule::CheckpointRouteAttr>(mappingAttr);
      auto fourState = mapping ? mapping.getFourState() : FlatSymbolRefAttr{};
      auto actor = mapping ? mapping.getActor() : IntegerAttr{};
      auto continuation = mapping ? mapping.getContinuation() : IntegerAttr{};
      if (!fourState || !actor || actor.getUInt() > UINT32_MAX ||
          !continuation || continuation.getUInt() == 0 ||
          continuation.getUInt() > UINT32_MAX)
        return module.emitError("has malformed eval checkpoint route"),
               failure();
      std::pair owner{static_cast<uint32_t>(actor.getUInt()),
                      static_cast<uint32_t>(continuation.getUInt())};
      auto [entry, inserted] =
          checkpointOwners.try_emplace(fourState.getValue(), owner);
      if (!inserted && entry->second != owner)
        return module.emitError("eval checkpoint route has multiple owners"),
               failure();
    }
    ::obelisk::schedule::remove<schedule::metadata::evalCheckpointRoutes>(
        module);
  }
  struct Route {
    LLVM::LLVMFuncOp fourState;
    LLVM::LLVMFuncOp twoState;
    LLVM::LLVMFuncOp pathKnownProbe;
    LLVM::LLVMFuncOp checkpointPathProbe;
    LLVM::LLVMFuncOp dispatcher;
    LLVM::LLVMFuncOp fourStateFallback;
    LLVM::LLVMFuncOp checkpointFallback;
    LLVM::LLVMFuncOp checkpointBody;
    DenseI64ArrayAttr ranges;
    bool independentEntry = false;
    std::optional<uint32_t> checkpointActor;
    std::optional<uint32_t> checkpointContinuation;
    std::string globalName;
    std::string dispatcherName;
    std::string fourStateFallbackName;
    std::string checkpointBodyName;
  };
  SmallVector<Route> routes;
  bool routeError = false;
  module.walk([&](LLVM::LLVMFuncOp function) {
    auto source = ::obelisk::schedule::get<
        ::obelisk::schedule::Field::EvalFourStateSource>(function);
    auto ranges = ::obelisk::schedule::get<
        ::obelisk::schedule::Field::EvalLocalPromotionRanges>(function);
    if (!source || !ranges)
      return;
    LLVM::LLVMFuncOp fourState =
        inputSymbols.lookup<LLVM::LLVMFuncOp>(source.getValue());
    if (!fourState || fourState.getFunctionType() != function.getFunctionType())
      return;
    LLVM::LLVMFuncOp pathKnownProbe;
    LLVM::LLVMFuncOp checkpointPathProbe;
    auto probeName = pathKnownProbes.find(function.getSymName());
    if (probeName != pathKnownProbes.end()) {
      pathKnownProbe = inputSymbols.lookup<LLVM::LLVMFuncOp>(probeName->second);
      if (!pathKnownProbe) {
        function.emitError("has no lowered path-known probe ")
            << probeName->second;
        routeError = true;
        return;
      }
      auto checkpointProbeName =
          checkpointPathProbes.find(function.getSymName());
      if (checkpointProbeName == checkpointPathProbes.end() ||
          !(checkpointPathProbe = inputSymbols.lookup<LLVM::LLVMFuncOp>(
                checkpointProbeName->second))) {
        function.emitError("has no lowered checkpoint path probe");
        routeError = true;
        return;
      }
    }
    size_t routeIndex = routes.size();
    std::optional<uint32_t> checkpointActor;
    std::optional<uint32_t> checkpointContinuation;
    if (pathKnownProbe) {
      auto owner = checkpointOwners.find(fourState.getSymName());
      if (owner == checkpointOwners.end()) {
        function.emitError("path-sensitive checkpoint route ")
            << fourState.getSymName() << " has no exact actor owner";
        routeError = true;
        return;
      }
      checkpointActor = owner->second.first;
      checkpointContinuation = owner->second.second;
    }
    routes.push_back(
        {fourState,
         function,
         pathKnownProbe,
         checkpointPathProbe,
         {},
         {},
         {},
         {},
         ranges,
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::EvalConditionallyTwoState>(function),
         checkpointActor,
         checkpointContinuation,
         (Twine("__obelisk_eval_function_route_v1_") + Twine(routeIndex)).str(),
         (Twine("__obelisk_eval_path_dispatch_v1_") + Twine(routeIndex)).str(),
         (Twine("__obelisk_eval_four_state_fallback_v1_") + Twine(routeIndex))
             .str(),
         (Twine("__obelisk_eval_checkpoint_body_v1_") + Twine(routeIndex))
             .str()});
  });
  if (routeError)
    return failure();
  if (routes.empty())
    return success();

  LLVM::LLVMFuncOp run =
      inputSymbols.lookup<LLVM::LLVMFuncOp>("__obelisk_aot_schedule_run_v1");
  if (!run || run.empty())
    return module.emitError("eval function routes have no AOT run wrapper");
  LLVM::CallOp prepare;
  LLVM::CallOp eventDrivenRun;
  run.walk([&](LLVM::CallOp call) {
    if (!call.getCallee())
      return;
    if (*call.getCallee() == "obelisk_rt_v1_scheduler_prepare_periodic_aot")
      prepare = call;
    else if (*call.getCallee() == "obelisk_rt_v1_scheduler_run_aot_nodes")
      eventDrivenRun = call;
  });
  if (!prepare && !eventDrivenRun) {
    auto scheduler =
        ::obelisk::schedule::get<::obelisk::schedule::Field::NativeScheduler>(
            module);
    if ((scheduler &&
         scheduler.getValue() == schedule::NativeSchedulerMode::Auto) ||
        ::obelisk::schedule::has<evalRuntimeNBAFallbackAttr>(module)) {
      ::obelisk::schedule::remove<evalRuntimeNBAFallbackAttr>(module);
      return success();
    }
    return run.emitError("eval function routes have no Tier-2 handoff");
  }
  // Periodic wrappers also contain a Tier-2 node-loop fallback. The presence
  // of that fallback does not change their periodic promotion contract.
  bool clocklessEval = !prepare && static_cast<bool>(eventDrivenRun);

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i64 = builder.getI64Type();
  constexpr StringLiteral routePromotionPendingName =
      "__obelisk_eval_route_promotion_pending_v1";
  constexpr StringLiteral routePromotionDirtyName =
      "__obelisk_eval_route_promotion_dirty_v1";
  constexpr StringLiteral routePromotionScanName =
      "__obelisk_eval_route_promotion_scan_v1";
  constexpr StringLiteral routePromotionBoundaryName =
      "__obelisk_eval_route_promotion_boundary_v1";
  const uint64_t routeWordCount = (routes.size() + 63) / 64;
  const bool needsRouteSummary = routeWordCount > 1;
  builder.setInsertionPointToStart(module.getBody());
  // One pending word is already its own nonempty summary. Avoid a redundant
  // dirty flag and calls to an empty scanner for small proof sets.
  if (needsRouteSummary)
    LLVM::GlobalOp::create(builder, module.getLoc(), i8, false,
                           LLVM::Linkage::Internal, routePromotionDirtyName,
                           builder.getI8IntegerAttr(1), 1);
  SmallVector<uint64_t> initialRoutePending(routeWordCount, 0);
  for (auto [index, route] : llvm::enumerate(routes))
    if (!route.pathKnownProbe &&
        (!route.ranges.empty() || route.independentEntry))
      initialRoutePending[index / 64] |= uint64_t{1} << (index % 64);
  auto pendingType = LLVM::LLVMArrayType::get(i64, routeWordCount);
  auto routePending = LLVM::GlobalOp::create(
      builder, module.getLoc(), pendingType, false, LLVM::Linkage::Internal,
      routePromotionPendingName, Attribute{}, 8);
  Block *pendingInitializer = new Block;
  routePending.getInitializerRegion().push_back(pendingInitializer);
  builder.setInsertionPointToStart(pendingInitializer);
  Value initialPending =
      LLVM::ZeroOp::create(builder, module.getLoc(), pendingType);
  for (auto [word, mask] : llvm::enumerate(initialRoutePending))
    initialPending = LLVM::InsertValueOp::create(
        builder, module.getLoc(), initialPending,
        detail::llvmConstant(builder, module.getLoc(), i64, mask),
        ArrayRef<int64_t>{static_cast<int64_t>(word)});
  LLVM::ReturnOp::create(builder, module.getLoc(), initialPending);
  auto resetRoutePending = [&](Location location) {
    for (auto [word, mask] : llvm::enumerate(initialRoutePending))
      LLVM::StoreOp::create(
          builder, location, detail::llvmConstant(builder, location, i64, mask),
          detail::byteGEP(builder, location,
                          LLVM::AddressOfOp::create(builder, location, pointer,
                                                    routePromotionPendingName),
                          word * sizeof(uint64_t)),
          8);
  };
  // IEEE 1800-2023 6.8: a variable retains its value between assignments.
  // Returning from a runtime checkpoint does not itself change knownness.
  // Plan installation invokes promotion_invalidate to seed routes and pending
  // proofs, including when a fresh context reuses this plan. Both periodic
  // and clockless runs retain that state across checkpoint re-entry; actual
  // X/Z stores still invalidate it.
  builder.setInsertionPointToStart(module.getBody());
  auto terminationRequested = inputSymbols.lookup<LLVM::LLVMFuncOp>(
      "obelisk_rt_v1_scheduler_termination_requested");
  if (!terminationRequested)
    terminationRequested = LLVM::LLVMFuncOp::create(
        builder, module.getLoc(),
        "obelisk_rt_v1_scheduler_termination_requested",
        LLVM::LLVMFunctionType::get(builder.getI32Type(), {pointer}));
  LLVM::LLVMFuncOp syncCheckpointStateFunction;
  for (auto [routeIndex, route] : llvm::enumerate(routes)) {
    if (route.pathKnownProbe) {
      auto probeType = route.pathKnownProbe.getFunctionType();
      auto bodyType = route.fourState.getFunctionType();
      if (probeType.getParams() != bodyType.getParams() ||
          probeType.getReturnType() != i8)
        return route.pathKnownProbe.emitError(
            "path-known probe ABI does not match its eval body");
      if (!route.checkpointPathProbe ||
          route.checkpointPathProbe.getFunctionType() != probeType)
        return route.twoState.emitError(
            "checkpoint path probe ABI does not match its eval body");
      if (bodyType.getReturnType() != builder.getI32Type())
        return route.twoState.emitError(
            "checkpointed eval body must return a runtime status");
      if (bodyType.getParams().size() != 1 ||
          bodyType.getParams().front() != pointer)
        return route.fourState.emitError(
            "checkpoint callback body must have type i32 (ptr)");

      // Preserve one cold copy outside the runtime-free evaluator closure.
      // The side-effect-free route probe selects this callback before either
      // generated body executes, so NBA staging and other prefix work run
      // exactly once in the runtime transaction.
      route.checkpointBody = cast<LLVM::LLVMFuncOp>(route.fourState->clone());
      route.checkpointBody.setSymName(route.checkpointBodyName);
      route.checkpointBody.setPrivate();
      module.getBody()->push_back(route.checkpointBody);

      auto fractureCheckpoints = [&](LLVM::LLVMFuncOp function) {
        llvm::MapVector<Block *, Operation *> checkpointBlocks;
        function.walk([&](LLVM::CallOp call) {
          if (std::optional<StringRef> callee = call.getCallee();
              callee && callee->starts_with("obelisk_rt_") &&
              *callee != "obelisk_rt_v1_coverage_point_hit" &&
              *callee != "obelisk_rt_v1_scheduler_termination_requested" &&
              *callee != "obelisk_rt_v1_scheduler_time" &&
              *callee != "obelisk_rt_v1_eval_display" &&
              *callee != "obelisk_rt_v1_eval_nba_reserve" &&
              *callee != "obelisk_rt_v1_scheduler_fail")
            checkpointBlocks.try_emplace(call->getBlock(), call.getOperation());
        });
        if (checkpointBlocks.empty())
          return success();
        for (auto [block, firstCheckpoint] : checkpointBlocks) {
          // Keep unsupported calls out of the generated closure. The route
          // probe must intercept this path before entering either body; this
          // return is a defensive failure path, not the runtime continuation.
          builder.setInsertionPoint(firstCheckpoint);
          for (Operation *operation = firstCheckpoint; operation;
               operation = operation->getNextNode())
            for (Value result : operation->getResults())
              if (!result.use_empty())
                result.replaceAllUsesWith(LLVM::PoisonOp::create(
                    builder, operation->getLoc(), result.getType()));
          for (Operation *operation = firstCheckpoint; operation;) {
            Operation *next = operation->getNextNode();
            operation->erase();
            operation = next;
          }
          builder.setInsertionPointToEnd(block);
          LLVM::ReturnOp::create(
              builder, function.getLoc(),
              detail::llvmConstant(builder, function.getLoc(),
                                   builder.getI32Type(),
                                   OBELISK_RT_INVALID_DESIGN));
        }
        IRRewriter rewriter(context);
        (void)eraseUnreachableBlocks(rewriter, function.getBody());
        return success();
      };
      if (failed(fractureCheckpoints(route.twoState)) ||
          failed(fractureCheckpoints(route.fourState)))
        return failure();

      LLVM::LLVMFuncOp resumeCoordinator =
          inputSymbols.lookup<LLVM::LLVMFuncOp>(detail::evalDispatchName);
      LLVM::GlobalOp mutableState = inputSymbols.lookup<LLVM::GlobalOp>(
          detail::evalCheckpointMutableStateName);
      if (!resumeCoordinator || !mutableState)
        return route.fourState.emitError(
            "checkpoint route has no same-slot generated resume");

      // The runtime invokes this cold thunk outside the generated hot call
      // graph. Execute the original activation once, then resume the same
      // generated slot coordinator so staged NBAs and downstream ready bits
      // reach the shared barrier before the next periodic edge.
      builder.setInsertionPointToEnd(module.getBody());
      route.checkpointFallback =
          LLVM::LLVMFuncOp::create(builder, route.fourState.getLoc(),
                                   route.fourStateFallbackName, bodyType);
      detail::copyNativePartition(route.fourState, route.checkpointFallback);
      route.checkpointFallback.setPrivate();
      Block *callbackEntry = route.checkpointFallback.addEntryBlock(builder);
      Block *resume = new Block;
      Block *publishNextCheckpoint = new Block;
      Block *returnResumeStatus = new Block;
      returnResumeStatus->addArgument(builder.getI32Type(),
                                      route.fourState.getLoc());
      Block *returnBodyStatus = new Block;
      returnBodyStatus->addArgument(builder.getI32Type(),
                                    route.fourState.getLoc());
      route.checkpointFallback.getBody().push_back(resume);
      route.checkpointFallback.getBody().push_back(publishNextCheckpoint);
      route.checkpointFallback.getBody().push_back(returnResumeStatus);
      route.checkpointFallback.getBody().push_back(returnBodyStatus);
      builder.setInsertionPointToStart(callbackEntry);
      // This callback executes the original four-state body outside the hot
      // closure. Preserve the same NBA provenance as an ordinary four-state
      // route before it can stage X/Z or publish downstream work.
      if (auto fallback = inputSymbols.lookup<LLVM::GlobalOp>(
              "__obelisk_eval_step_four_state_fallback_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fallback.getSymName()),
            1);
      if (auto fastRoots = inputSymbols.lookup<LLVM::GlobalOp>(
              "__obelisk_eval_fast_nba_roots_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            LLVM::ZeroOp::create(builder, route.fourState.getLoc(),
                                 fastRoots.getGlobalType()),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fastRoots.getSymName()),
            8);
      SmallVector<Value> callbackArguments(callbackEntry->getArguments());
      Value bodyStatus =
          LLVM::CallOp::create(builder, route.fourState.getLoc(),
                               route.checkpointBody, callbackArguments)
              .getResult();
      Value bodyOK = LLVM::ICmpOp::create(
          builder, route.fourState.getLoc(), LLVM::ICmpPredicate::eq,
          bodyStatus,
          detail::llvmConstant(builder, route.fourState.getLoc(),
                               builder.getI32Type(), OBELISK_RT_OK));
      // A taken $finish/$fatal (or batch-mode $stop) has already ended the
      // active phase. Re-entering the generated same-slot coordinator here
      // can execute another ready activation before its post-body termination
      // check, or commit an NBA that the generic finish transaction discards.
      // Synchronize the planes and let the runtime enter final procedures.
      // This query is confined to the cold callback, not the generated loop.
      Value terminating =
          LLVM::CallOp::create(builder, route.fourState.getLoc(),
                               terminationRequested,
                               ValueRange{callbackEntry->getArgument(0)})
              .getResult();
      Value continuing = LLVM::ICmpOp::create(
          builder, route.fourState.getLoc(), LLVM::ICmpPredicate::eq,
          terminating,
          detail::llvmConstant(builder, route.fourState.getLoc(),
                               builder.getI32Type(), 0));
      bodyOK = LLVM::AndOp::create(builder, route.fourState.getLoc(), bodyOK,
                                   continuing);
      LLVM::CondBrOp::create(builder, route.fourState.getLoc(), bodyOK, resume,
                             ValueRange{}, returnBodyStatus,
                             ValueRange{bodyStatus});
      builder.setInsertionPointToStart(resume);
      Value mutableStateValue = LLVM::LoadOp::create(
          builder, route.fourState.getLoc(), pointer,
          LLVM::AddressOfOp::create(builder, route.fourState.getLoc(), pointer,
                                    mutableState.getSymName()),
          8);
      Value resumeStatus =
          LLVM::CallOp::create(
              builder, route.fourState.getLoc(), resumeCoordinator,
              ValueRange{mutableStateValue, callbackEntry->getArgument(0)})
              .getResult();
      Value resumedAtCheckpoint = LLVM::ICmpOp::create(
          builder, route.fourState.getLoc(), LLVM::ICmpPredicate::eq,
          resumeStatus,
          detail::llvmConstant(builder, route.fourState.getLoc(),
                               builder.getI32Type(),
                               OBELISK_RT_AOT_GENERATED_CHECKPOINT));
      LLVM::CondBrOp::create(builder, route.fourState.getLoc(),
                             resumedAtCheckpoint, publishNextCheckpoint,
                             ValueRange{}, returnResumeStatus,
                             ValueRange{resumeStatus});
      builder.setInsertionPointToStart(publishNextCheckpoint);
      Value nextActor = LLVM::LoadOp::create(
          builder, route.fourState.getLoc(), builder.getI32Type(),
          LLVM::AddressOfOp::create(builder, route.fourState.getLoc(), pointer,
                                    detail::evalCheckpointActorName),
          4);
      Value nextContinuation = LLVM::LoadOp::create(
          builder, route.fourState.getLoc(), builder.getI32Type(),
          LLVM::AddressOfOp::create(builder, route.fourState.getLoc(), pointer,
                                    detail::evalCheckpointContinuationName),
          4);
      Value nextCallback = LLVM::LoadOp::create(
          builder, route.fourState.getLoc(), pointer,
          LLVM::AddressOfOp::create(builder, route.fourState.getLoc(), pointer,
                                    detail::evalCheckpointCallbackName),
          8);
      Value queueStatus =
          LLVM::CallOp::create(
              builder, route.fourState.getLoc(),
              TypeRange{builder.getI32Type()},
              SymbolRefAttr::get(
                  context, "obelisk_rt_v1_scheduler_queue_aot_checkpoint"),
              ValueRange{callbackEntry->getArgument(0), nextActor,
                         nextContinuation, nextCallback})
              .getResult();
      Value queued = LLVM::ICmpOp::create(
          builder, route.fourState.getLoc(), LLVM::ICmpPredicate::eq,
          queueStatus,
          detail::llvmConstant(builder, route.fourState.getLoc(),
                               builder.getI32Type(), OBELISK_RT_OK));
      Value publishedStatus = LLVM::SelectOp::create(
          builder, route.fourState.getLoc(), queued, resumeStatus, queueStatus);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             publishedStatus);
      builder.setInsertionPointToStart(returnResumeStatus);
      // The cold body and same-slot coordinator commit into generated planes.
      // Publish those commits before the runtime drains the slot and exports
      // its canonical image, otherwise that export restores pre-checkpoint
      // values (and the next activation repeats the same checkpoint forever).
      auto syncCheckpointState = [&] {
        auto stateBits =
            module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
        if (!syncCheckpointStateFunction)
          syncCheckpointStateFunction = detail::getOrDeclareLLVMFunction(
              module, "obelisk_rt_v1_native_state_sync", builder.getI32Type(),
              {pointer, pointer, pointer, builder.getI64Type()});
        return LLVM::CallOp::create(
                   builder, route.fourState.getLoc(),
                   TypeRange{builder.getI32Type()},
                   SymbolRefAttr::get(context,
                                      "obelisk_rt_v1_native_state_sync"),
                   ValueRange{callbackEntry->getArgument(0),
                              LLVM::AddressOfOp::create(
                                  builder, route.fourState.getLoc(), pointer,
                                  "__obelisk_state_value"),
                              LLVM::AddressOfOp::create(
                                  builder, route.fourState.getLoc(), pointer,
                                  "__obelisk_state_unknown"),
                              detail::llvmConstant(
                                  builder, route.fourState.getLoc(),
                                  builder.getI64Type(), stateBits.getUInt())})
            .getResult();
      };
      Value syncStatus = syncCheckpointState();
      Value resumeOK = LLVM::ICmpOp::create(
          builder, route.fourState.getLoc(), LLVM::ICmpPredicate::eq,
          returnResumeStatus->getArgument(0),
          detail::llvmConstant(builder, route.fourState.getLoc(),
                               builder.getI32Type(), OBELISK_RT_OK));
      LLVM::ReturnOp::create(
          builder, route.fourState.getLoc(),
          LLVM::SelectOp::create(builder, route.fourState.getLoc(), resumeOK,
                                 syncStatus,
                                 returnResumeStatus->getArgument(0)));
      builder.setInsertionPointToStart(returnBodyStatus);
      (void)syncCheckpointState();
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             returnBodyStatus->getArgument(0));

      builder.setInsertionPointToEnd(module.getBody());
      route.dispatcher = LLVM::LLVMFuncOp::create(
          builder, route.twoState.getLoc(), route.dispatcherName, bodyType);
      detail::copyNativePartition(route.twoState, route.dispatcher);
      route.dispatcher.setPrivate();
      route.dispatcher->setAttr(
          "passthrough",
          builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
      Block *entry = route.dispatcher.addEntryBlock(builder);
      Block *twoState = new Block;
      Block *fourState = new Block;
      Block *fullProbe = new Block;
      Block *knownStateProbe = new Block;
      Block *probeJoin = new Block;
      probeJoin->addArgument(i8, route.twoState.getLoc());
      Block *classifyNative = new Block;
      Block *checkpoint = new Block;
      Block *checkpointClearUnknown = new Block;
      Block *checkpointPublish = new Block;
      route.dispatcher.getBody().push_back(twoState);
      route.dispatcher.getBody().push_back(fourState);
      route.dispatcher.getBody().push_back(fullProbe);
      route.dispatcher.getBody().push_back(knownStateProbe);
      route.dispatcher.getBody().push_back(probeJoin);
      route.dispatcher.getBody().push_back(classifyNative);
      route.dispatcher.getBody().push_back(checkpoint);
      route.dispatcher.getBody().push_back(checkpointClearUnknown);
      route.dispatcher.getBody().push_back(checkpointPublish);
      builder.setInsertionPointToStart(entry);
      SmallVector<Value> arguments(entry->getArguments());
      Value promoted = detail::llvmConstant(builder, route.twoState.getLoc(),
                                             builder.getI1Type(), 0);
      if (clocklessEval)
        promoted = LLVM::ICmpOp::create(
            builder, route.twoState.getLoc(), LLVM::ICmpPredicate::ne,
            LLVM::LoadOp::create(
                builder, route.twoState.getLoc(), i8,
                LLVM::AddressOfOp::create(builder, route.twoState.getLoc(),
                    pointer, "__obelisk_eval_promotion_latched_v1"), 1),
            detail::llvmConstant(builder, route.twoState.getLoc(), i8, 0));
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), promoted,
                             knownStateProbe, fullProbe);
      builder.setInsertionPointToStart(fullProbe);
      Value fullPath = LLVM::CallOp::create(builder, route.twoState.getLoc(),
                                            route.pathKnownProbe, arguments)
                           .getResult();
      LLVM::BrOp::create(builder, route.twoState.getLoc(), ValueRange{fullPath},
                         probeJoin);
      builder.setInsertionPointToStart(knownStateProbe);
      Value knownPath =
          LLVM::CallOp::create(builder, route.twoState.getLoc(),
                               route.checkpointPathProbe, arguments)
              .getResult();
      LLVM::BrOp::create(builder, route.twoState.getLoc(),
                         ValueRange{knownPath}, probeJoin);
      builder.setInsertionPointToStart(probeJoin);
      Value path = probeJoin->getArgument(0);
      Value isCheckpoint = LLVM::ICmpOp::create(
          builder, route.twoState.getLoc(), LLVM::ICmpPredicate::eq, path,
          detail::llvmConstant(builder, route.twoState.getLoc(), i8, 2));
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), isCheckpoint,
                             checkpoint, classifyNative);
      builder.setInsertionPointToStart(classifyNative);
      Value known = LLVM::ICmpOp::create(
          builder, route.twoState.getLoc(), LLVM::ICmpPredicate::eq, path,
          detail::llvmConstant(builder, route.twoState.getLoc(), i8, 1));
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), known, twoState,
                             fourState);
      builder.setInsertionPointToStart(checkpoint);
      // A promoted body executes against a certified two-state view and
      // deliberately leaves the canonical unknown plane untouched.  Before
      // handing a checkpoint back to its four-state continuation, materialize
      // that view by clearing exactly the owner's certified state ranges.  A
      // checkpoint reached through the unpromoted four-state probe must retain
      // its real X/Z bits.
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), promoted,
                             checkpointClearUnknown, checkpointPublish);
      builder.setInsertionPointToStart(checkpointClearUnknown);
      Value checkpointUnknown = LLVM::AddressOfOp::create(
          builder, route.twoState.getLoc(), pointer, "__obelisk_state_unknown");
      ArrayRef<int64_t> checkpointRanges = route.ranges.asArrayRef();
      if ((checkpointRanges.size() & 1) != 0)
        return route.twoState.emitError("malformed local promotion ranges");
      for (size_t index = 0; index != checkpointRanges.size(); index += 2) {
        if (checkpointRanges[index] < 0 || checkpointRanges[index + 1] <= 0)
          return route.twoState.emitError("invalid local promotion range");
        uint64_t bitOffset = static_cast<uint64_t>(checkpointRanges[index]);
        uint64_t bitWidth = static_cast<uint64_t>(checkpointRanges[index + 1]);
        uint64_t firstByte = bitOffset / 8;
        uint64_t lastBit = bitOffset + bitWidth;
        uint64_t lastByte = (lastBit + 7) / 8;
        for (uint64_t byte = firstByte; byte != lastByte; ++byte) {
          uint8_t mask = UINT8_MAX;
          if (byte == firstByte && bitOffset % 8 != 0)
            mask &= static_cast<uint8_t>(UINT8_MAX << (bitOffset % 8));
          if (byte + 1 == lastByte && lastBit % 8 != 0)
            mask &= static_cast<uint8_t>((uint16_t{1} << (lastBit % 8)) - 1);
          Value address = detail::byteGEP(builder, route.twoState.getLoc(),
                                          checkpointUnknown, byte);
          if (mask == UINT8_MAX) {
            LLVM::StoreOp::create(
                builder, route.twoState.getLoc(),
                detail::llvmConstant(builder, route.twoState.getLoc(), i8, 0),
                address, 1);
          } else {
            Value old = LLVM::LoadOp::create(builder, route.twoState.getLoc(),
                                             i8, address, 1);
            LLVM::StoreOp::create(
                builder, route.twoState.getLoc(),
                LLVM::AndOp::create(
                    builder, route.twoState.getLoc(), old,
                    detail::llvmConstant(builder, route.twoState.getLoc(), i8,
                                         static_cast<uint8_t>(~mask))),
                address, 1);
          }
        }
      }
      LLVM::BrOp::create(builder, route.twoState.getLoc(), ValueRange{},
                         checkpointPublish);
      builder.setInsertionPointToStart(checkpointPublish);
      LLVM::StoreOp::create(
          builder, route.twoState.getLoc(),
          detail::llvmConstant(builder, route.twoState.getLoc(),
                               builder.getI32Type(), *route.checkpointActor),
          LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                    detail::evalCheckpointActorName),
          4);
      LLVM::StoreOp::create(
          builder, route.twoState.getLoc(),
          detail::llvmConstant(builder, route.twoState.getLoc(),
                               builder.getI32Type(),
                               *route.checkpointContinuation),
          LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                    detail::evalCheckpointContinuationName),
          4);
      LLVM::StoreOp::create(
          builder, route.twoState.getLoc(),
          LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                    route.checkpointFallback.getSymName()),
          LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                    detail::evalCheckpointCallbackName),
          8);
      LLVM::ReturnOp::create(
          builder, route.twoState.getLoc(),
          detail::llvmConstant(builder, route.twoState.getLoc(),
                               builder.getI32Type(),
                               OBELISK_RT_AOT_GENERATED_CHECKPOINT));
      auto emitTailCall = [&](Block *block, LLVM::LLVMFuncOp callee,
                              bool fourStateFallback) {
        builder.setInsertionPointToStart(block);
        if (fourStateFallback) {
          // The predicate has proven that this body has no blocking
          // publication. Record the local four-state route before executing
          // it so the shared NBA barrier preserves its staged unknown plane.
          if (auto fallback = inputSymbols.lookup<LLVM::GlobalOp>(
                  "__obelisk_eval_step_four_state_fallback_v1"))
            LLVM::StoreOp::create(
                builder, route.fourState.getLoc(),
                detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
                LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                          pointer, fallback.getSymName()),
                1);
          // Domain changes are published at actual canonical stores.
          // Staging a four-state payload cannot revoke destination proofs.
        }
        LLVM::CallOp call = LLVM::CallOp::create(
            builder, route.twoState.getLoc(), callee, arguments);
        LLVM::ReturnOp::create(builder, route.twoState.getLoc(),
                               call.getResults());
      };
      emitTailCall(twoState, route.twoState, false);
      emitTailCall(fourState, route.fourState, true);
    } else {
      // Indirect route selection must report a four-state leaf to the shared
      // NBA barrier. Point the cold route at a wrapper that records the
      // fallback before entering the model body; promotion replaces the
      // route with the two-state body directly, so the hot edge stays clean.
      auto bodyType = route.fourState.getFunctionType();
      builder.setInsertionPointToEnd(module.getBody());
      route.fourStateFallback =
          LLVM::LLVMFuncOp::create(builder, route.fourState.getLoc(),
                                   route.fourStateFallbackName, bodyType);
      detail::copyNativePartition(route.fourState, route.fourStateFallback);
      route.fourStateFallback.setPrivate();
      Block *entry = route.fourStateFallback.addEntryBlock(builder);
      builder.setInsertionPointToStart(entry);
      if (auto fallback = inputSymbols.lookup<LLVM::GlobalOp>(
              "__obelisk_eval_step_four_state_fallback_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fallback.getSymName()),
            1);
      // Domain changes are published at actual canonical stores.
      // Staging a four-state payload cannot revoke destination proofs.
      SmallVector<Value> arguments(entry->getArguments());
      LLVM::CallOp call = LLVM::CallOp::create(
          builder, route.fourState.getLoc(), route.fourState, arguments);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             call.getResults());
    }
    builder.setInsertionPointToStart(module.getBody());
    auto global = LLVM::GlobalOp::create(
        builder, route.twoState.getLoc(), pointer, false,
        LLVM::Linkage::Internal, route.globalName, Attribute{}, 8);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(
        builder, route.twoState.getLoc(),
        LLVM::ZeroOp::create(builder, route.twoState.getLoc(), pointer));

    // Installation seeds routes before draining startup. Checkpoint re-entry
    // preserves selections (IEEE 1800-2023 6.8); promotion follows successful
    // preparation/coordinator exits and invalidation follows canonical writes.
    StringRef fallback = route.dispatcher ? route.dispatcher.getSymName()
                         : route.fourStateFallback
                             ? route.fourStateFallback.getSymName()
                             : route.fourState.getSymName();
    if (!route.pathKnownProbe && !route.ranges.empty())
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalRouteProofDependencies>(
          global,
          schedule::RouteProofDependencyAttr::get(
              builder.getContext(), FlatSymbolRefAttr::get(context, fallback),
              route.ranges, builder.getI64IntegerAttr(routeIndex)));
    ArrayRef<int64_t> encoded = route.ranges.asArrayRef();
    if ((encoded.size() & 1) != 0)
      return route.twoState.emitError("malformed local promotion ranges");
    for (size_t index = 0; index != encoded.size(); index += 2)
      if (encoded[index] < 0 || encoded[index + 1] <= 0)
        return route.twoState.emitError("invalid local promotion range");
  }

  // Actual canonical unknown-plane changes request a boundary scan through
  // the reverse proof index. Merely committing an NBA does not disturb any
  // certificate: payloads and untouched X/Z bits are not new evidence.

  // Keep the potentially large masked scan out of line. Its tiny boundary
  // wrapper is inlined into generated coordinators, so a clean clock pays no
  // call and no per-route promotion guards.
  builder.setInsertionPointToEnd(module.getBody());
  auto routePromotionScan = LLVM::LLVMFuncOp::create(
      builder, module.getLoc(), routePromotionScanName,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context), {}, false));
  routePromotionScan->setAttr(
      "passthrough", builder.getArrayAttr({builder.getStringAttr("noinline"),
                                           builder.getStringAttr("cold")}));
  Block *routeScanEntry = routePromotionScan.addEntryBlock(builder);
  builder.setInsertionPointToStart(routeScanEntry);
  if (needsRouteSummary)
    LLVM::StoreOp::create(builder, module.getLoc(),
                          detail::llvmConstant(builder, module.getLoc(), i8, 0),
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer,
                                                    routePromotionDirtyName),
                          1);
  for (uint64_t word = 0; word != routeWordCount; ++word) {
    Location location = module.getLoc();
    Value address =
        detail::byteGEP(builder, location,
                        LLVM::AddressOfOp::create(builder, location, pointer,
                                                  routePromotionPendingName),
                        word * sizeof(uint64_t));
    Value pending = LLVM::LoadOp::create(builder, location, i64, address, 8);
    Block *inspect = new Block, *nextWord = new Block;
    routePromotionScan.getBody().push_back(inspect);
    routePromotionScan.getBody().push_back(nextWord);
    LLVM::CondBrOp::create(
        builder, location,
        LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::ne,
                             pending,
                             detail::llvmConstant(builder, location, i64, 0)),
        inspect, nextWord);
    builder.setInsertionPointToStart(inspect);
    // No actor, observer, or foreign call runs during a proof scan. Consume
    // this word once; newly invalidated proofs are queued at actual mutations.
    LLVM::StoreOp::create(builder, location,
                          detail::llvmConstant(builder, location, i64, 0),
                          address, 8);
    uint64_t end = std::min<uint64_t>(routes.size(), (word + 1) * 64);
    for (uint64_t index = word * 64; index != end; ++index) {
      Route &route = routes[index];
      if (route.pathKnownProbe ||
          (route.ranges.empty() && !route.independentEntry))
        continue;
      Block *scan = new Block, *nextRoute = new Block;
      routePromotionScan.getBody().push_back(scan);
      routePromotionScan.getBody().push_back(nextRoute);
      Value selectedBit = LLVM::AndOp::create(
          builder, location, pending,
          detail::llvmConstant(builder, location, i64,
                               uint64_t{1} << (index % 64)));
      LLVM::CondBrOp::create(
          builder, location,
          LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::ne,
                               selectedBit,
                               detail::llvmConstant(builder, location, i64, 0)),
          scan, nextRoute);
      builder.setInsertionPointToStart(scan);
      Location location = route.twoState.getLoc();
      Value unknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                "__obelisk_state_unknown");
      Value anyUnknown = detail::llvmConstant(builder, location, i8, 0);
      ArrayRef<int64_t> encoded = route.ranges.asArrayRef();
      for (size_t index = 0; index != encoded.size(); index += 2) {
        uint64_t bitOffset = static_cast<uint64_t>(encoded[index]);
        uint64_t bitWidth = static_cast<uint64_t>(encoded[index + 1]);
        uint64_t firstByte = bitOffset / 8;
        uint64_t lastBit = bitOffset + bitWidth;
        uint64_t lastByte = (lastBit + 7) / 8;
        for (uint64_t byte = firstByte; byte != lastByte; ++byte) {
          uint8_t mask = UINT8_MAX;
          if (byte == firstByte && bitOffset % 8 != 0)
            mask &= static_cast<uint8_t>(UINT8_MAX << (bitOffset % 8));
          if (byte + 1 == lastByte && lastBit % 8 != 0)
            mask &= static_cast<uint8_t>((uint16_t{1} << (lastBit % 8)) - 1);
          Value bits = LLVM::LoadOp::create(
              builder, location, i8,
              detail::byteGEP(builder, location, unknown, byte), 1);
          if (mask != UINT8_MAX)
            bits = LLVM::AndOp::create(
                builder, location, bits,
                detail::llvmConstant(builder, location, i8, mask));
          anyUnknown = LLVM::OrOp::create(builder, location, anyUnknown, bits);
        }
      }
      Value known =
          encoded.empty()
              ? detail::llvmConstant(builder, location, builder.getI1Type(),
                                     true)
              : LLVM::ICmpOp::create(
                    builder, location, LLVM::ICmpPredicate::eq, anyUnknown,
                    detail::llvmConstant(builder, location, i8, 0));
      StringRef fallback = route.dispatcher ? route.dispatcher.getSymName()
                           : route.fourStateFallback
                               ? route.fourStateFallback.getSymName()
                               : route.fourState.getSymName();
      Value selected = LLVM::SelectOp::create(
          builder, location, known,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    route.twoState.getSymName()),
          LLVM::AddressOfOp::create(builder, location, pointer, fallback));
      LLVM::StoreOp::create(builder, location, selected,
                            LLVM::AddressOfOp::create(
                                builder, location, pointer, route.globalName),
                            8);
      LLVM::BrOp::create(builder, location, ValueRange{}, nextRoute);
      builder.setInsertionPointToStart(nextRoute);
    }
    LLVM::BrOp::create(builder, location, ValueRange{}, nextWord);
    builder.setInsertionPointToStart(nextWord);
  }
  LLVM::ReturnOp::create(builder, module.getLoc(), ValueRange{});

  builder.setInsertionPointToEnd(module.getBody());
  Type i32 = builder.getI32Type();
  auto routePromotionBoundary = LLVM::LLVMFuncOp::create(
      builder, module.getLoc(), routePromotionBoundaryName,
      LLVM::LLVMFunctionType::get(i32, {i32}, false));
  routePromotionBoundary->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *boundaryEntry = routePromotionBoundary.addEntryBlock(builder);
  Block *boundaryScan = new Block;
  Block *boundaryReturn = new Block;
  routePromotionBoundary.getBody().push_back(boundaryScan);
  routePromotionBoundary.getBody().push_back(boundaryReturn);
  builder.setInsertionPointToStart(boundaryEntry);
  Value statusOK = LLVM::ICmpOp::create(
      builder, module.getLoc(), LLVM::ICmpPredicate::eq,
      boundaryEntry->getArgument(0),
      detail::llvmConstant(builder, module.getLoc(), i32, OBELISK_RT_OK));
  Type summaryType = needsRouteSummary ? i8 : i64;
  StringRef summaryName =
      needsRouteSummary ? routePromotionDirtyName : routePromotionPendingName;
  Value dirty = LLVM::LoadOp::create(
      builder, module.getLoc(), summaryType,
      LLVM::AddressOfOp::create(builder, module.getLoc(), pointer, summaryName),
      needsRouteSummary ? 1 : 8);
  Value isDirty = LLVM::ICmpOp::create(
      builder, module.getLoc(), LLVM::ICmpPredicate::ne, dirty,
      detail::llvmConstant(builder, module.getLoc(), summaryType, 0));
  LLVM::CondBrOp::create(
      builder, module.getLoc(),
      LLVM::AndOp::create(builder, module.getLoc(), statusOK, isDirty),
      boundaryScan, boundaryReturn);
  builder.setInsertionPointToStart(boundaryScan);
  LLVM::CallOp::create(builder, module.getLoc(), routePromotionScan,
                       ValueRange{});
  LLVM::BrOp::create(builder, module.getLoc(), ValueRange{}, boundaryReturn);
  builder.setInsertionPointToStart(boundaryReturn);
  LLVM::ReturnOp::create(builder, module.getLoc(),
                         boundaryEntry->getArgument(0));

  // Periodic preparation may drain startup or a checkpoint. Scan only the
  // proofs invalidated by actual writes, and only after a successful boundary.
  // A failed preparation must not rescan every route on each checkpoint retry.
  if (prepare) {
    builder.setInsertionPointAfter(prepare);
    auto boundaryCall =
        LLVM::CallOp::create(builder, prepare.getLoc(), routePromotionBoundary,
                             ValueRange{prepare.getResult()});
    for (OpOperand &use :
         llvm::make_early_inc_range(prepare.getResult().getUses()))
      if (use.getOwner() != boundaryCall.getOperation())
        use.set(boundaryCall.getResult());
  }
  SmallVector<LLVM::ReturnOp> coordinatorReturns;
  if (LLVM::LLVMFuncOp coordinator =
          inputSymbols.lookup<LLVM::LLVMFuncOp>(detail::evalDispatchName))
    coordinator.walk([&](LLVM::ReturnOp returnOp) {
      if (returnOp.getNumOperands() == 1)
        coordinatorReturns.push_back(returnOp);
    });
  for (LLVM::ReturnOp returnOp : coordinatorReturns) {
    builder.setInsertionPoint(returnOp);
    auto boundaryCall =
        LLVM::CallOp::create(builder, returnOp.getLoc(), routePromotionBoundary,
                             ValueRange{returnOp.getOperand(0)});
    returnOp->setOperand(0, boundaryCall.getResult());
  }

  llvm::StringMap<Route *> routesByFunction;
  for (Route &route : routes) {
    routesByFunction[route.fourState.getSymName()] = &route;
    routesByFunction[route.twoState.getSymName()] = &route;
  }

  // An asynchronous X/Z handoff clears the model-wide promotion latch. Reset
  // every non-vacuous local route in the same generated invalidator so the
  // four-state coordinator cannot retain a stale two-state leaf selection.
  if (LLVM::LLVMFuncOp invalidate = inputSymbols.lookup<LLVM::LLVMFuncOp>(
          "__obelisk_eval_promotion_invalidate_v1")) {
    SmallVector<LLVM::ReturnOp> returns;
    invalidate.walk(
        [&](LLVM::ReturnOp returnOp) { returns.push_back(returnOp); });
    for (LLVM::ReturnOp returnOp : returns) {
      builder.setInsertionPoint(returnOp);
      resetRoutePending(returnOp.getLoc());
      if (needsRouteSummary)
        LLVM::StoreOp::create(
            builder, returnOp.getLoc(),
            detail::llvmConstant(builder, returnOp.getLoc(), i8, 1),
            LLVM::AddressOfOp::create(builder, returnOp.getLoc(), pointer,
                                      routePromotionDirtyName),
            1);
      for (Route &route : routes) {
        StringRef fallback = route.dispatcher ? route.dispatcher.getSymName()
                             : route.fourStateFallback
                                 ? route.fourStateFallback.getSymName()
                                 : route.fourState.getSymName();
        LLVM::StoreOp::create(
            builder, returnOp.getLoc(),
            LLVM::AddressOfOp::create(builder, returnOp.getLoc(), pointer,
                                      fallback),
            LLVM::AddressOfOp::create(builder, returnOp.getLoc(), pointer,
                                      route.globalName),
            8);
      }
    }
  }

  // A whole-model proof may fail while independent routes are already
  // promotable. At this quiescent query, consume the same pending local
  // proofs used by other boundaries; a global latch is not their evidence.
  if (LLVM::LLVMFuncOp promotion = inputSymbols.lookup<LLVM::LLVMFuncOp>(
          "__obelisk_eval_promotion_ready_v1")) {
    SmallVector<LLVM::ReturnOp> promotionReturns;
    promotion.walk([&](LLVM::ReturnOp returnOp) {
      if (returnOp.getNumOperands() == 1 &&
          !isa_and_nonnull<LLVM::ConstantOp>(
              returnOp.getOperand(0).getDefiningOp()))
        promotionReturns.push_back(returnOp);
    });
    for (LLVM::ReturnOp returnOp : promotionReturns) {
      builder.setInsertionPoint(returnOp);
      LLVM::CallOp::create(
          builder, returnOp.getLoc(), routePromotionBoundary,
          ValueRange{detail::llvmConstant(builder, returnOp.getLoc(), i32,
                                          OBELISK_RT_OK)});
    }
  }

  // Consume the selected owner's proof at its call boundary. Sharing a
  // mutable wrapper with four-state entries must not reintroduce a route
  // lookup after this exact call has already selected the two-state body.
  // Only the small wrapper is cloned; computation and scheduling stay shared.
  // Include the helpers emitted above and register each clone in this same
  // table. Rebuilding a module symbol table per clone is quadratic.
  SymbolTable wrapperSymbols(module);
  SmallVector<LLVM::CallOp> trustedWrapperCalls;
  module.walk([&](LLVM::CallOp call) {
    if (!::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalProvenTwoStateCall>(call) ||
        !call.getCallee())
      return;
    auto callee = wrapperSymbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
    if (callee &&
        ::obelisk::schedule::has<schedule::Field::EvalTwoStateWrapper>(callee))
      trustedWrapperCalls.push_back(call);
  });
  llvm::DenseMap<Operation *, LLVM::LLVMFuncOp> trustedWrapperClones;
  for (LLVM::CallOp call : trustedWrapperCalls) {
    LLVM::LLVMFuncOp wrapper =
        wrapperSymbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
    LLVM::LLVMFuncOp &clone = trustedWrapperClones[wrapper.getOperation()];
    if (!clone) {
      bool recursive = false;
      wrapper.walk([&](LLVM::CallOp nested) {
        recursive |=
            nested.getCallee() && *nested.getCallee() == wrapper.getSymName();
      });
      if (recursive)
        return wrapper.emitError(
            "cannot specialize a recursive trusted eval wrapper");
      SmallString<128> base(wrapper.getSymName());
      base.append(".__obelisk_trusted");
      unsigned suffix = 0;
      SmallString<128> name(base);
      while (wrapperSymbols.lookup<LLVM::LLVMFuncOp>(name)) {
        name = base;
        (Twine("_") + Twine(++suffix)).toVector(name);
      }
      Operation *detached = wrapper->clone();
      clone = cast<LLVM::LLVMFuncOp>(detached);
      clone.setSymName(name);
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalTrustedTwoStateClosure>(
          clone, builder.getUnitAttr());
      wrapperSymbols.insert(detached);
    }
    call.setCallee(clone.getSymName());
  }

  SmallVector<LLVM::CallOp> calls;
  module.walk([&](LLVM::CallOp call) {
    if (call.getCallee() && routesByFunction.contains(*call.getCallee()))
      calls.push_back(call);
  });
  for (LLVM::CallOp call : calls) {
    Route &route = *routesByFunction.lookup(*call.getCallee());
    // Certified ranked groups already selected this exact value-domain
    // branch. Replacing it with a mutable route would undo the boundary proof
    // and reintroduce an indirect dispatch for each internal computation.
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalGroupDomainSelected>(call))
      continue;
    LLVM::LLVMFuncOp caller = call->getParentOfType<LLVM::LLVMFuncOp>();
    if (caller &&
        (caller == route.dispatcher || caller == route.fourStateFallback))
      continue;
    // A path-guarded route never changes its entry function: the dispatcher
    // itself performs the exact two-state/four-state/checkpoint decision.
    // Preserve a direct symbol edge so normal LLVM inlining can outline by
    // module instance without an indirect function-pointer load per clock.
    if (route.dispatcher) {
      call.setCallee(route.dispatcher.getSymName());
      continue;
    }
    bool selectedTwoStateClosure =
        caller &&
        (::obelisk::schedule::has<
             ::obelisk::schedule::Field::EvalFourStateSource>(caller) ||
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::EvalTrustedTwoStateClosure>(caller));
    if (selectedTwoStateClosure && !route.dispatcher) {
      // The selected call has already established the owner proof
      // at its execution boundary. Preserve a direct edge so LLVM can inline across
      // module instances, but retain a nested owner's path predicate: the
      // outer closure certificate deliberately excludes ranges owned by that
      // independently guarded checkpoint.
      call.setCallee(route.twoState.getSymName());
      continue;
    }
    builder.setInsertionPoint(call);
    Value selected = LLVM::LoadOp::create(
        builder, call.getLoc(), pointer,
        LLVM::AddressOfOp::create(builder, call.getLoc(), pointer,
                                  route.globalName),
        8);
    SmallVector<Value> operands{selected};
    llvm::append_range(operands, call.getArgOperands());
    auto replacement = LLVM::CallOp::create(
        builder, call.getLoc(), route.fourState.getFunctionType(), operands);
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalAllowedCallees>(
        replacement, builder.getArrayAttr([&] {
          SmallVector<Attribute> allowed{
              FlatSymbolRefAttr::get(context, route.twoState.getSymName())};
          if (!route.pathKnownProbe)
            allowed.push_back(
                FlatSymbolRefAttr::get(context, route.fourState.getSymName()));
          if (route.dispatcher)
            allowed.push_back(
                FlatSymbolRefAttr::get(context, route.dispatcher.getSymName()));
          if (route.fourStateFallback)
            allowed.push_back(FlatSymbolRefAttr::get(
                context, route.fourStateFallback.getSymName()));
          return allowed;
        }()));
    call.replaceAllUsesWith(replacement.getResults());
    call.erase();
  }

  // Consume the inductive two-state proof all the way through the canonical
  // state ABI. Earlier packed lowering normally folds these accesses, but
  // module-instance wrappers and late inlining can retain raw LLVM plane
  // operations. The compatibility value/unknown layout remains canonical at
  // handoffs; proven two-state generated bodies neither read nor write its
  // unknown plane.
  auto isUnknownPlaneAddress = [](Value address) {
    while (address) {
      if (auto global = address.getDefiningOp<LLVM::AddressOfOp>())
        return global.getGlobalName() == "__obelisk_state_unknown";
      if (auto gep = address.getDefiningOp<LLVM::GEPOp>()) {
        address = gep.getBase();
        continue;
      }
      return false;
    }
    return false;
  };
  SmallVector<LLVM::LoadOp> unknownLoads;
  SmallVector<LLVM::StoreOp> unknownStores;
  module.walk([&](LLVM::LLVMFuncOp function) {
    bool twoState =
        ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalFourStateSource>(function) ||
        ::obelisk::schedule::has<schedule::Field::EvalTwoStateWrapper>(
            function) ||
        ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalSelectedTwoState>(function);
    if (!twoState)
      return;
    function.walk([&](Operation *operation) {
      if (auto load = dyn_cast<LLVM::LoadOp>(operation);
          load && isUnknownPlaneAddress(load.getAddr()))
        unknownLoads.push_back(load);
      else if (auto store = dyn_cast<LLVM::StoreOp>(operation);
               store && isUnknownPlaneAddress(store.getAddr()))
        unknownStores.push_back(store);
    });
  });
  for (LLVM::LoadOp load : unknownLoads) {
    builder.setInsertionPoint(load);
    load.replaceAllUsesWith(
        LLVM::ZeroOp::create(builder, load.getLoc(), load.getType())
            .getResult());
    load.erase();
  }
  for (LLVM::StoreOp store : unknownStores)
    store.erase();

  return success();
}

LogicalResult materializeEvalTwoStateNBACommit(ModuleOp module) {
  constexpr StringLiteral fourStateName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral twoStateName =
      "__obelisk_aot_static_nba_commit_two_state_v1";
  constexpr StringLiteral fastTwoStateName =
      "__obelisk_aot_static_nba_commit_two_state_fast_v1";
  LLVM::LLVMFuncOp source =
      module.lookupSymbol<LLVM::LLVMFuncOp>(fourStateName);
  if (!source)
    return success();

  OpBuilder builder(source);
  builder.setInsertionPointAfter(source);
  auto clone = cast<LLVM::LLVMFuncOp>(builder.clone(*source.getOperation()));
  clone.setSymName(twoStateName);
  clone.setLinkage(LLVM::Linkage::Internal);
  clone->setAttr(
      "passthrough",
      ArrayAttr::get(module.getContext(),
                     {StringAttr::get(module.getContext(), "noinline")}));

  SmallVector<LLVM::LoadOp> stagedUnknownLoads;
  clone.walk([&](Operation *operation) {
    if (auto load = dyn_cast<LLVM::LoadOp>(operation);
        load && ::obelisk::schedule::has<
                    ::obelisk::schedule::Field::EvalTwoStateZeroUnknown>(load))
      stagedUnknownLoads.push_back(load);
  });
  for (LLVM::LoadOp load : stagedUnknownLoads) {
    builder.setInsertionPoint(load);
    load.replaceAllUsesWith(
        LLVM::ZeroOp::create(builder, load.getLoc(), load.getType())
            .getResult());
    load.erase();
  }
  builder.setInsertionPointAfter(clone);
  auto fastClone = cast<LLVM::LLVMFuncOp>(builder.clone(*clone.getOperation()));
  fastClone.setSymName(fastTwoStateName);
  // This compact value-plane-only barrier is part of the Tier-1 slot
  // coordinator, not a handoff boundary. Leave it to normal profitability:
  // forcing a large fixed-root barrier into run_until inflates the hot loop
  // without improving the generated schedule. Keep the canonical and
  // four-state barriers explicitly out of line below.
  fastClone->removeAttr("passthrough");
  // The promoted coordinator enters this clone only for the NBA update
  // region.  AOT partitioning deliberately keeps the (large) barrier out of
  // the coordinator's object, so LLVM cannot propagate that constant across
  // the call boundary.  Specialize it here instead: otherwise every scalar
  // root repeats an exec-region comparison on every clock.
  builder.setInsertionPointToStart(&fastClone.getBody().front());
  fastClone.getBody().front().getArgument(2).replaceAllUsesWith(
      detail::llvmConstant(builder, fastClone.getLoc(), builder.getI32Type(),
                           2));
  // The full four-state and canonicalizing two-state barriers are handoff
  // paths.  Keep them out of the promoted coordinator so their unknown-plane
  // bookkeeping does not inflate register pressure and instruction layout in
  // the fast barrier selected after the one-time canonical scan.
  source->setAttr(
      "passthrough",
      ArrayAttr::get(module.getContext(),
                     {StringAttr::get(module.getContext(), "noinline")}));
  SmallVector<LLVM::LoadOp> canonicalUnknownLoads;
  SmallVector<LLVM::StoreOp> canonicalUnknownStores;
  auto isUnknownPlaneAddress = [](Value address) {
    while (address) {
      if (auto global = address.getDefiningOp<LLVM::AddressOfOp>())
        return global.getGlobalName() == "__obelisk_state_unknown";
      if (auto gep = address.getDefiningOp<LLVM::GEPOp>()) {
        address = gep.getBase();
        continue;
      }
      return false;
    }
    return false;
  };
  fastClone.walk([&](Operation *operation) {
    // Fixed-root promotion evidence says nothing about a per-site latch's
    // selected destination. It can overwrite previously unknown bits, even
    // when its new value is known and the fixed dirty bitmap is empty. Keep
    // both planes for these publications until a separate destination proof
    // exists; preserving only the value would leave stale canonical X bits.
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalPreserveNbaUnknown>(operation))
      return;
    if (auto load = dyn_cast<LLVM::LoadOp>(operation);
        load && isUnknownPlaneAddress(load.getAddr()))
      canonicalUnknownLoads.push_back(load);
    else if (auto store = dyn_cast<LLVM::StoreOp>(operation);
             store && isUnknownPlaneAddress(store.getAddr()))
      canonicalUnknownStores.push_back(store);
  });
  for (LLVM::LoadOp load : canonicalUnknownLoads) {
    builder.setInsertionPoint(load);
    load.replaceAllUsesWith(
        LLVM::ZeroOp::create(builder, load.getLoc(), load.getType())
            .getResult());
    load.erase();
  }
  for (LLVM::StoreOp store : canonicalUnknownStores)
    store.erase();
  for (LLVM::LLVMFuncOp function : {source, clone, fastClone})
    function.walk([](Operation *operation) {
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalPreserveNbaUnknown>(operation);
    });

  // Specialization is carried entirely by call-site intent.  Validate and
  // consume the phase-local markers so a renamed or newly outlined
  // coordinator cannot silently retain the wrong NBA implementation.
  WalkResult rewrite = module.walk([&](LLVM::CallOp call) {
    bool useFast = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::EvalUseFastTwoStateNba>(call);
    bool useCanonical = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::EvalUseCanonicalTwoStateNba>(call);
    bool keepFourState = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::EvalKeepFourStateNba>(call);
    unsigned intents = useFast + useCanonical + keepFourState;
    if (intents == 0)
      return WalkResult::advance();
    if (intents != 1)
      return call.emitError("NBA call has conflicting specialization intents"),
             WalkResult::interrupt();
    std::optional<StringRef> callee = call.getCallee();
    if (!callee)
      return call.emitError("NBA specialization requires a direct call"),
             WalkResult::interrupt();
    bool expectedSource = *callee == fourStateName;
    bool expectedCanonical = *callee == twoStateName;
    bool expectedFast = *callee == fastTwoStateName;
    if (useFast) {
      if (!expectedSource && !expectedCanonical && !expectedFast)
        return call.emitError("fast two-state NBA intent is attached to an "
                              "unrelated callee"),
               WalkResult::interrupt();
      auto region = call.getArgOperands()[2].getDefiningOp<LLVM::ConstantOp>();
      auto regionValue =
          region ? dyn_cast<IntegerAttr>(region.getValue()) : IntegerAttr{};
      if (!regionValue || regionValue.getInt() != 2)
        return call.emitError("fast two-state NBA intent requires the NBA "
                              "execution region"),
               WalkResult::interrupt();
      call.setCallee(fastTwoStateName);
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalUseFastTwoStateNba>(call);
      return WalkResult::advance();
    }
    if (useCanonical) {
      if (!expectedSource && !expectedCanonical && !expectedFast)
        return call.emitError("canonical two-state NBA intent is attached to "
                              "an unrelated callee"),
               WalkResult::interrupt();
      call.setCallee(twoStateName);
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalUseCanonicalTwoStateNba>(call);
      return WalkResult::advance();
    }
    if (!expectedSource)
      return call.emitError("four-state NBA intent is attached to an "
                            "unrelated callee"),
             WalkResult::interrupt();
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalKeepFourStateNba>(call);
    return WalkResult::advance();
  });
  if (rewrite.wasInterrupted())
    return failure();
  source.walk([](LLVM::LoadOp load) {
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalTwoStateZeroUnknown>(load);
  });
  return success();
}

class ConvertObeliskSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertObeliskSimProcessesToLLVMCoroutinesPassBase<
          ConvertObeliskSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    if (failed(verifyFunctionalCoverageSchemaBeforeBackend(module)))
      return signalPassFailure();
    bool detailedTiming = module->hasAttr("obelisk.debug.native_timing");
    auto lastTiming = std::chrono::steady_clock::now();
    auto markTiming = [&](StringRef name) {
      if (!detailedTiming)
        return;
      auto now = std::chrono::steady_clock::now();
      double seconds = std::chrono::duration<double>(now - lastTiming).count();
      llvm::errs() << "obelisk native timing: " << name << ": " << seconds
                   << " s\n";
      lastTiming = now;
    };
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "coroutine lowering requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    unsigned pointerBits = parsed->getPointerSizeInBits();
    if (!parsed->isLittleEndian() || (pointerBits != 32 && pointerBits != 64)) {
      module.emitError("coroutine lowering requires a little-endian target "
                       "with 32-bit or 64-bit pointers");
      return signalPassFailure();
    }
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();
    FailureOr<analysis::NativeStateLayoutAnalysis> embeddedStateLayout =
        analysis::NativeStateLayoutAnalysis::compute(module);
    if (failed(embeddedStateLayout))
      return signalPassFailure();
    uint64_t stateBits = embeddedStateLayout->bitCount;
    if (auto existing =
            module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
        existing && existing.getValue().getZExtValue() != stateBits) {
      module.emitError(
          "native state layout disagrees with embedded execution metadata");
      return signalPassFailure();
    }
    module->setAttr(
        "obelisk.execution.state_bits",
        IntegerAttr::get(IntegerType::get(&getContext(), 64), stateBits));
    if (failed(materializeEmbeddedSimulationDesign(module, *parsed)))
      return signalPassFailure();
    markTiming("validation and embedded design");

    if (failed(prepareSimulationProcessesToLLVMCoroutines(module, *parsed)))
      return signalPassFailure();
    markTiming("native process preparation");

    LowerToLLVMOptions options(&getContext());
    options.dataLayout = *parsed;
    LLVMTypeConverter converter(&getContext(), options);
    converter.addConversion([&](Type type) -> std::optional<Type> {
      Type converted = convertProcessType(type, &getContext());
      if (converted != type)
        return converted;
      return std::nullopt;
    });
    addRuntimeToLLVMTypeConversions(converter);
    if (failed(prepareRuntimeToLLVMByteGlobals(module))) {
      signalPassFailure();
      return;
    }
    markTiming("runtime byte-global inventory");

    // Runtime materializers may create immutable byte globals and runtime
    // calls may declare their C ABI entry points. Lower them serially before
    // converting independent function bodies so parallel workers only mutate
    // their own functions and symbol/global order remains deterministic.
    {
      RewritePatternSet runtimePatterns(&getContext());
      populateRuntimeToLLVMPatterns(converter, runtimePatterns);
      ConversionTarget runtimeTarget(getContext());
      runtimeTarget.addLegalDialect<LLVM::LLVMDialect>();
      runtimeTarget.addLegalOp<ModuleOp>();
      runtimeTarget.addIllegalDialect<runtime::ObeliskRuntimeDialect>();
      runtimeTarget.markUnknownOpDynamicallyLegal(
          [](Operation *) { return true; });
      if (failed(applyPartialConversion(module, runtimeTarget,
                                        std::move(runtimePatterns)))) {
        signalPassFailure();
        return;
      }
    }
    markTiming("serial runtime conversion");

    // Integer power is intentionally kept compact through packed-value type
    // conversion. Expand it only now, when its operands are ordinary integer
    // planes and CFG construction no longer runs inside one-to-many dialect
    // conversion.
    SmallVector<Operation *> integerPowers;
    module.walk([&](math::IPowIOp power) {
      integerPowers.push_back(power.getOperation());
    });
    RewritePatternSet integerPowerPatterns(&getContext());
    integerPowerPatterns.add<ExpandIntegerPower>(&getContext());
    GreedyRewriteConfig integerPowerConfig;
    integerPowerConfig.setStrictness(GreedyRewriteStrictness::ExistingOps)
        .setRegionSimplificationLevel(GreedySimplifyRegionLevel::Disabled)
        .enableFolding(false)
        .enableConstantCSE(false);
    if (failed(applyOpPatternsGreedily(
            integerPowers,
            FrozenRewritePatternSet(std::move(integerPowerPatterns)),
            integerPowerConfig))) {
      signalPassFailure();
      return;
    }
    markTiming("integer power expansion");

    RewritePatternSet patterns(&getContext());
    populateSimulationCoroutineBodyToLLVMPatterns(converter, patterns);
    if (failed(verify(module)))
      return signalPassFailure();
    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp, UnrealizedConversionCastOp>();
    target.markUnknownOpDynamicallyLegal(
        [](Operation *operation) { return isa<LLVM::LLVMFuncOp>(operation); });
    FrozenRewritePatternSet frozenPatterns(std::move(patterns));

    // Native preparation leaves function signatures in their final physical
    // ABI and all cross-function symbols frozen. Convert independent bodies
    // concurrently before the inexpensive serial wrapper conversion. A
    // module-wide conversion driver otherwise walks thousands of cold UVM
    // methods serially and dominates -O3 compile time.
    SmallVector<SmallVector<Operation *>> functionBodies;
    Dialect *llvmDialect = getContext().getLoadedDialect<LLVM::LLVMDialect>();
    module.walk([&](FunctionOpInterface function) {
      if (function.isExternal())
        return;
      WalkResult inventory = function.walk([&](Operation *operation) {
        if (operation != function.getOperation() &&
            operation->getDialect() != llvmDialect)
          return WalkResult::interrupt();
        return WalkResult::advance();
      });
      if (!inventory.wasInterrupted())
        return;
      SmallVector<Operation *> roots;
      for (Block &block : function.getFunctionBody())
        for (Operation &operation : block)
          roots.push_back(&operation);
      if (!roots.empty())
        functionBodies.push_back(std::move(roots));
    });
    markTiming("function-body inventory");

    // Amortize converter and pattern construction without sharing their
    // mutable type-conversion caches between threads. Keeping chunks bounded
    // also distributes the uneven UVM method sizes across workers.
    constexpr size_t functionsPerChunk = 64;
    SmallVector<SmallVector<Operation *>> chunks;
    chunks.reserve((functionBodies.size() + functionsPerChunk - 1) /
                   functionsPerChunk);
    for (auto [index, roots] : llvm::enumerate(functionBodies)) {
      if (index % functionsPerChunk == 0)
        chunks.emplace_back();
      chunks.back().append(roots);
    }
    // Body conversion cannot add, replace, or rename module symbols. Share a
    // lookup index only for this phase; the subsequent wrapper conversion may
    // replace function operations and must use its own uncached patterns.
    SymbolTableCollection bodySymbols;
    bodySymbols.getSymbolTable(module);
    LockedSymbolTableCollection lockedBodySymbols(bodySymbols);
    if (failed(failableParallelForEach(
            &getContext(), chunks, [&](ArrayRef<Operation *> roots) {
              LowerToLLVMOptions workerOptions(&getContext());
              workerOptions.dataLayout = *parsed;
              LLVMTypeConverter workerConverter(&getContext(), workerOptions);
              workerConverter.addConversion(
                  [&](Type type) -> std::optional<Type> {
                    Type converted = convertProcessType(type, &getContext());
                    if (converted != type)
                      return converted;
                    return std::nullopt;
                  });
              addRuntimeToLLVMTypeConversions(workerConverter);
              RewritePatternSet workerPatterns(&getContext());
              populateSimulationCoroutineBodyToLLVMPatterns(
                  workerConverter, workerPatterns, &lockedBodySymbols);
              FrozenRewritePatternSet workerFrozen(std::move(workerPatterns));
              ConversionTarget workerTarget(getContext());
              workerTarget.addLegalDialect<LLVM::LLVMDialect>();
              workerTarget.addLegalOp<UnrealizedConversionCastOp>();
              workerTarget.markUnknownOpDynamicallyLegal(
                  [](Operation *operation) {
                    return isa<LLVM::LLVMFuncOp>(operation);
                  });
              return applyFullConversion(roots, workerTarget, workerFrozen);
            }))) {
      signalPassFailure();
      return;
    }
    markTiming("parallel function-body conversion");
    if (failed(applyFullConversion(module, target, frozenPatterns))) {
      signalPassFailure();
      return;
    }
    markTiming("serial wrapper conversion");
    // IEEE 1800-2017 31.7 condition descriptors have no process-visible
    // value. Fragment extraction may discard the suspension after packed
    // conversion; remove only its now-dead tagged bridge before the standard
    // conversion-cast reconciliation below.
    SmallVector<UnrealizedConversionCastOp> deadObserverBridges;
    module.walk([&](UnrealizedConversionCastOp cast) {
      if (cast->hasAttr("obelisk.coro.observer_id") && cast->use_empty())
        deadObserverBridges.push_back(cast);
    });
    for (UnrealizedConversionCastOp cast : deadObserverBridges)
      cast.erase();
    SmallVector<UnrealizedConversionCastOp> unrealizedCasts;
    module.walk([&](UnrealizedConversionCastOp cast) {
      unrealizedCasts.push_back(cast);
    });
    SmallVector<UnrealizedConversionCastOp> remainingCasts;
    reconcileUnrealizedCasts(unrealizedCasts, &remainingCasts);
    if (!remainingCasts.empty()) {
      remainingCasts.front().emitError(
          "failed to reconcile staged runtime type conversion");
      signalPassFailure();
      return;
    }
    markTiming("conversion cast reconciliation");
    if (failed(materializeEvalFunctionRoutes(module))) {
      signalPassFailure();
      return;
    }
    markTiming("eval function route materialization");
    if (failed(detail::materializeNativePromotionRangeIndex(module))) {
      signalPassFailure();
      return;
    }
    markTiming("promotion range index materialization");
    if (failed(materializeEvalTwoStateNBACommit(module))) {
      signalPassFailure();
      return;
    }
    markTiming("two-state NBA commit materialization");
    if (failed(materializeNativeObserverThunks(module))) {
      signalPassFailure();
      return;
    }
    if (failed(materializeNativeDPIExportThunks(module))) {
      signalPassFailure();
      return;
    }
    markTiming("observer and DPI thunk materialization");
    if (failed(detail::materializeNativePromotionWrites(module))) {
      signalPassFailure();
      return;
    }
    markTiming("promotion write materialization");
    if (failed(detail::materializeNativeEvalGroupBodies(module))) {
      signalPassFailure();
      return;
    }
    markTiming("native activation group materialization");
    if (failed(verifyGeneratedEvalCallClosures(module))) {
      signalPassFailure();
      return;
    }
    markTiming("eval call closure verification");
    if (failed(detail::finalizeNativePartitionManifest(module))) {
      signalPassFailure();
      return;
    }
    markTiming("post-conversion materialization");
    // The remaining rewrites only inspect and mutate one function. Run them
    // as a nested pass so MLIR owns scheduling and the single-threaded path
    // uses precisely the same transformation.
    auto optimizationLevel =
        module->getAttrOfType<IntegerAttr>("obelisk.native.optimization_level");
    auto limitAttr =
        ::obelisk::schedule::get<::obelisk::schedule::Field::MaxInlineOps>(
            module);
    uint64_t inlineOperationLimit =
        limitAttr ? limitAttr.getValue().getZExtValue() : UINT64_C(5000);
    if (!optimizationLevel || optimizationLevel.getInt() < 2)
      inlineOperationLimit = 0;
    OpPassManager finalization(ModuleOp::getOperationName());
    finalization.nest<LLVM::LLVMFuncOp>().addPass(
        detail::createNativeFunctionFinalizationPass(*parsed,
                                                     inlineOperationLimit));
    if (failed(runPipeline(finalization, module))) {
      signalPassFailure();
      return;
    }
    module->removeAttr("obelisk.native.optimization_level");
    ::obelisk::schedule::remove<::obelisk::schedule::Field::MaxInlineOps>(
        module);
    module->removeAttr("obelisk.native.max_state_domain_functions");
    module->removeAttr("obelisk.debug.native_timing");
    markTiming("native function finalization");
    if (failed(verify(module)))
      signalPassFailure();
    else
      markTiming("final verification");
  }
};

} // namespace

LogicalResult
prepareSimulationProcessesToLLVMCoroutines(ModuleOp module,
                                           const llvm::DataLayout &dataLayout) {
  return prepareSimulationProcessesForLLVMCoroutinesImpl(module, dataLayout);
}

void populateSimulationCoroutineToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns) {
  populateRuntimeToLLVMPatterns(converter, patterns);
  populateSimulationCoroutineBodyToLLVMPatterns(converter, patterns);
}

static void populateSimulationCoroutineBodyToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns,
    SymbolTableCollection *symbolTables) {
  populateContextRuntimeToLLVMConversionPattern(patterns, converter);
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateMathToLLVMConversionPatterns(converter, patterns);
  // Every IEEE 1800-2017 Table 20-4 real math function reaches an LLVM
  // intrinsic through the patterns above except the inverse hyperbolics, which
  // have none. Name those three so the math dialect's own expansions supply
  // them, and so the expansions that would displace an intrinsic stay out.
  math::populateExpansionPatterns(patterns, {"asinh", "acosh", "atanh"});
  populateSCFToControlFlowConversionPatterns(patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns, symbolTables);
}

} // namespace obelisk
