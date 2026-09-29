//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "../SimulationToSchedule/NativePipeline.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

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
#include "obelisk/Analysis/SimulationCopyProcessAnalysis.h"
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
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
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
#include "mlir/Transforms/InliningUtils.h"
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

#define GEN_PASS_DEF_CONVERTPREPAREDSIMPROCESSESTOLLVMCOROUTINESPASS
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

using detail::buildNativeStateLayout;
using detail::convertProcessType;
using detail::declareNativeRuntimeABI;
using detail::declareTableProcessRuntimeABI;
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
using detail::materializeDPIThunks;
using detail::materializeGeneratedNBAAccumulators;
using detail::materializeManagedMethodThunks;
using detail::materializeNativeDPIExportThunks;
using detail::materializeNativeObserverThunks;
using detail::materializeNativePeriodicClockPlan;
using detail::materializeNativeSchedulerGlobals;
using detail::materializeNativeStatePlanes;
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
              dyn_cast<schedule::NativeSuspendChangeOp>(block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<schedule::NativeSuspendEdgeOp>(
                   block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<schedule::NativeSuspendAnyOp>(
                   block->getTerminator()))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<schedule::NativeSuspendObserveOp>(
                   block->getTerminator()))
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
        if (auto suspend = dyn_cast<schedule::NativeSuspendChangeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendEdgeOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendAnyOp>(operation))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendObserveOp>(operation))
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
      if (!isa<schedule::NativeSuspendChangeOp, schedule::NativeSuspendEdgeOp,
               schedule::NativeSuspendAnyOp, schedule::NativeSuspendObserveOp>(
              operation))
        return;
      schedule::ContinuationSiteAttr site;
      if (auto suspend = dyn_cast<schedule::NativeSuspendChangeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend =
                   dyn_cast<schedule::NativeSuspendEdgeOp>(operation))
        site = suspend.getSiteAttr();
      else if (auto suspend = dyn_cast<schedule::NativeSuspendAnyOp>(operation))
        site = suspend.getSiteAttr();
      else
        site = cast<schedule::NativeSuspendObserveOp>(operation).getSiteAttr();
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
        if (auto suspend =
                dyn_cast<schedule::NativeSuspendChangeOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendEdgeOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendAnyOp>(terminator))
          site = suspend.getSiteAttr();
        else if (auto suspend =
                     dyn_cast<schedule::NativeSuspendObserveOp>(terminator))
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
      uint64_t nextCodeUnit = 1;
      uint64_t scope = 0;
      for (auto declaration : design.getOps<sim::SimCodeUnitDeclOp>()) {
        nextCodeUnit = std::max(nextCodeUnit, declaration.getId() + 1);
        if (actor.getCodeUnitId() &&
            declaration.getId() == *actor.getCodeUnitId())
          scope = declaration.getScopeId();
      }
      body.setCodeUnitIdAttr(builder.getI64IntegerAttr(nextCodeUnit));
      sim::SimCodeUnitDeclOp::create(
          builder, actor.getLoc(), nextCodeUnit, scope,
          sim::EntryKind::Function, builder.getStringAttr(bodyName),
          builder.getStringAttr("native direct fragment"),
          builder.getUnitAttr());
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

// Native code inside the simulation symbol table imports the exact LLVM
// declarations it uses. Flattening the design later merges those declarations
// with their module definitions, as for any pair of LLVM symbol scopes.
LogicalResult declareNativeImports(ModuleOp module) {
  SymbolTable moduleSymbols(module);
  SymbolTableCollection symbols;
  SmallVector<std::pair<sim::SimDesignOp, Operation *>> imports;
  llvm::DenseSet<std::pair<Operation *, Operation *>> seen;
  module.walk([&](Operation *operation) {
    auto design = operation->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return;
    FlatSymbolRefAttr reference;
    if (auto address = dyn_cast<LLVM::AddressOfOp>(operation))
      reference = address.getGlobalNameAttr();
    else if (auto call = dyn_cast<LLVM::CallOp>(operation))
      reference = call.getCalleeAttr();
    if (!reference || symbols.lookupSymbolIn(design, reference))
      return;
    Operation *definition = moduleSymbols.lookup(reference.getValue());
    if (definition && seen.insert({design, definition}).second)
      imports.emplace_back(design, definition);
  });
  OpBuilder builder(module.getContext());
  for (auto [design, definition] : imports) {
    builder.setInsertionPointToStart(&design.getBody().front());
    if (auto function = dyn_cast<LLVM::LLVMFuncOp>(definition)) {
      auto declaration = LLVM::LLVMFuncOp::create(builder, function.getLoc(),
                                                  function.getSymName(),
                                                  function.getFunctionType());
      declaration.setCConv(function.getCConv());
    } else if (auto global = dyn_cast<LLVM::GlobalOp>(definition)) {
      LLVM::GlobalOp::create(builder, global.getLoc(), global.getGlobalType(),
                             global.getConstant(), LLVM::Linkage::External,
                             global.getSymName(), Attribute{},
                             global.getAlignment().value_or(0));
    } else {
      return definition->emitError(
          "native import must be an LLVM function or global");
    }
  }
  return success();
}

} // namespace
namespace detail {
void NativePipelineAnalysis::markTiming(StringRef name) {
  if (!detailedTiming)
    return;
  auto now = std::chrono::steady_clock::now();
  double seconds = std::chrono::duration<double>(now - lastTiming).count();
  llvm::errs() << "obelisk native preparation timing: " << name << ": "
               << seconds << " s\n";
  lastTiming = now;
}
LogicalResult NativePipelineAnalysis::initialize() {
  detailedTiming = module->hasAttr("obelisk.debug.native_timing");
  lastTiming = std::chrono::steady_clock::now();
  if (failed(verifyFunctionalCoverageSchemaBeforeBackend(module)))
    return failure();
  auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
  if (!layoutAttr) {
    module.emitError(
        "coroutine lowering requires an explicit llvm.data_layout");
    return failure();
  }
  llvm::Expected<llvm::DataLayout> parsed =
      llvm::DataLayout::parse(layoutAttr.getValue());
  if (!parsed) {
    module.emitError() << "invalid LLVM data layout: "
                       << llvm::toString(parsed.takeError());
    return failure();
  }
  unsigned pointerBits = parsed->getPointerSizeInBits();
  if (!parsed->isLittleEndian() || (pointerBits != 32 && pointerBits != 64)) {
    module.emitError("coroutine lowering requires a little-endian target "
                     "with 32-bit or 64-bit pointers");
    return failure();
  }
  dataLayout = *parsed;
  if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
    return failure();
  FailureOr<analysis::NativeStateLayoutAnalysis> embeddedStateLayout =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(embeddedStateLayout))
    return failure();
  uint64_t stateBits = embeddedStateLayout->bitCount;
  if (auto existing =
          module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
      existing && existing.getValue().getZExtValue() != stateBits) {
    module.emitError(
        "native state layout disagrees with embedded execution metadata");
    return failure();
  }
  module->setAttr("obelisk.execution.state_bits",
                  IntegerAttr::get(IntegerType::get(context, 64), stateBits));
  if (failed(materializeEmbeddedSimulationDesign(module, *parsed)))
    return failure();
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
  stateLayout = buildNativeStateLayout(module);
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
  module.walk([&](sim::SimDesignOp design) {
    metadataDesign = design;
    staticSuperstep =
        ::obelisk::schedule::get<schedule::metadata::staticSuperstep>(design);
  });
  auto executionFlags =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.flags");
  bytecodeOnly = executionFlags && (executionFlags.getValue().getZExtValue() &
                                    OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
  if (bytecodeOnly) {
    materializeNativeStatePlanes(module, *stateLayout);
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
    if (failed(makeSchedulerMain(module, *stateLayout, false, false, false)))
      return failure();

    // The executable bodies are frozen in the design image. Keep only the
    // metadata-derived globals, root spawn shell, DPI callbacks, and scheduler
    // entry point for native lowering.
    metadataDesign.erase();
    return success();
  }

  return success();
}

LogicalResult NativePipelineAnalysis::prepareFrames() {
  if (bytecodeOnly)
    return success();
  materializeNativeStatePlanes(module, *stateLayout);
  materializeNativeSchedulerGlobals(module);
  declareNativeRuntimeABI(module);
  declareProcessSpawnRuntimeABI(module);
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    if (analysis::isCaptureCopyProcess(function))
      copyActivations.insert(function);
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
    // IEEE 1800-2023 4.9.1/4.9.6: retain port-copy activation and publication.
    if (!copyActivations.contains(function))
      if (auto plan = detail::analyzeTableProcess(function, **analysis))
        tableProcesses.try_emplace(function, std::move(*plan));
    for (const ProcessSuspension &suspension : (*analysis)->getSuspensions()) {
      ::obelisk::schedule::set<::obelisk::schedule::Field::NativeContinuation>(
          suspension.operation, IntegerAttr::get(IntegerType::get(context, 32),
                                                 suspension.continuationID));
      ::obelisk::schedule::set<::obelisk::schedule::Field::NativeWaitOffset>(
          suspension.operation, IntegerAttr::get(IntegerType::get(context, 64),
                                                 suspension.waitOffset));
      ::obelisk::schedule::set<::obelisk::schedule::Field::NativeWaitSize>(
          suspension.operation,
          IntegerAttr::get(IntegerType::get(context, 64), suspension.waitSize));
    }
    analyses.insert({function.getOperation(), std::move(*analysis)});
    return WalkResult::advance();
  });
  if (analyzed.wasInterrupted())
    return failure();
  markTiming("frame analysis and state threading");
  if (detailedTiming)
    llvm::errs() << "obelisk native copy activations: "
                 << copyActivations.size() << '\n';
  if (detailedTiming)
    llvm::errs() << "obelisk native table candidates: " << tableProcesses.size()
                 << '\n';

  return success();
}

LogicalResult NativePipelineAnalysis::prepareRoots() {
  if (bytecodeOnly)
    return success();
  if (materializeNBAAccumulators &&
      failed(materializeGeneratedNBAAccumulators(module, staticNBAPlan)))
    return failure();
  if (failed(materializeNativePeriodicClockPlan(module, periodicClocks)))
    return failure();
  scheduleRanks = analysis::SimulationScheduleAnalysis::compute(module);
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
  guardedAOTSpecialization =
      staticSpecialization && useAOT && aotEligibility.isFullyEligible() &&
      vpi.allowsWrite() && (directStaticState || staticNBA);
  // Writable VPI can invalidate specialization between activations. Keep the
  // original coroutine bodies guarded: they are also the transactional
  // fallback bodies, so marking them permanently clean would suppress the
  // transition publications needed after an external deposit.

  return declareNativeImports(module);
}

LogicalResult NativePipelineAnalysis::prepareFragments() {
  if (bytecodeOnly)
    return success();
  // Fragment specialization can remove a suspension while leaving its
  // binding token unused in the generated eval body.
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

  bool enableDirectStaticState = directStaticState;
  if (failed(lowerPackedSimulationOperations(
          module, dataLayout, *stateLayout, enableDirectStaticState,
          staticNBA ? &staticNBAPlan : nullptr, vpi.allowsWrite(),
          /*experimentalTwoState=*/false)))
    return failure();
  // Constructor edges were consumed when class allocation was lowered.
  module.walk([](sim::SimClassDeclOp declaration) {
    declaration.removeImplicitConstructorAttr();
  });
  // An observer's scalar semantic result can expand to two native planes.
  // Complete its ordinary-function conversion at this representation boundary.
  SmallVector<sim::SimFuncOp> observers;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Observer)
      observers.push_back(function);
  });
  for (auto observer : observers) {
    auto prepared = prepareOrdinaryFunction(observer);
    if (failed(prepared) || failed(lowerPreparedOrdinaryFunction(*prepared)))
      return failure();
    SmallVector<func::CallOp> calls;
    prepared->body.walk([&](func::CallOp call) { calls.push_back(call); });
    for (auto call : calls) {
      OpBuilder builder(call);
      auto native = schedule::NativeExecuteOp::create(
          builder, call.getLoc(), call.getResultTypes(), call.getCalleeAttr(),
          call.getOperands(), UnitAttr{});
      call.replaceAllUsesWith(native.getResults());
      call.erase();
    }
  }
  SmallVector<sim::SimCallOp> observerCalls;
  module.walk([&](sim::SimCallOp call) {
    if (SymbolTable::lookupNearestSymbolFrom<func::FuncOp>(
            call, call.getCalleeAttr()))
      observerCalls.push_back(call);
  });
  for (auto call : observerCalls) {
    OpBuilder builder(call);
    SmallVector<Value> arguments;
    for (Value value : call.getOperands()) {
      if (isa<sim::ContextType>(value.getType()))
        value = schedule::NativeContextOp::create(
            builder, call.getLoc(), LLVM::LLVMPointerType::get(context), value);
      else if (isa<sim::TimeType>(value.getType()))
        value = schedule::NativeTimeOp::create(builder, call.getLoc(),
                                               builder.getI64Type(), value);
      arguments.push_back(value);
    }
    auto native = schedule::NativeExecuteOp::create(
        builder, call.getLoc(), call.getResultTypes(), call.getCalleeAttr(),
        arguments, UnitAttr{});
    call.replaceAllUsesWith(native.getResults());
    call.erase();
  }
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk(
      [&](sim::SimClassMethodDeclOp method) { methods.push_back(method); });
  for (auto method : methods) {
    OpBuilder builder(method);
    OperationState state(method.getLoc(),
                         schedule::NativeMethodOp::getOperationName());
    state.addAttributes(method->getAttrs());
    auto native = cast<schedule::NativeMethodOp>(builder.create(state));
    if (method.getImplementationAttr()) {
      auto implementation =
          SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
              method, method.getImplementationAttr());
      if (!implementation)
        return method.emitError("native method implementation is missing");
      native.setNativeTypeAttr(TypeAttr::get(implementation.getFunctionType()));
    }
    method.erase();
  }
  module.walk([&](LLVM::CallOp call) {
    OpBuilder builder(call);
    for (OpOperand &operand : call->getOpOperands()) {
      if (isa<sim::ContextType>(operand.get().getType()))
        operand.set(schedule::NativeContextOp::create(
            builder, call.getLoc(), LLVM::LLVMPointerType::get(context),
            operand.get()));
      else if (isa<sim::TimeType>(operand.get().getType()))
        operand.set(schedule::NativeTimeOp::create(
            builder, call.getLoc(), builder.getI64Type(), operand.get()));
    }
  });
  markTiming("packed simulation lowering");

  directFragments = materializeDirectFragments(
      module, metadataDesign, aotActorSlotsByCodeUnit, analyses,
      aotBytecodeContinuations, preLowerGeneratedRegionCodeUnits,
      runtimeCheckpointContinuations,
      useAOT && cleanSuperstep && staticFanoutPlan.exact &&
          (cleanWritableEval || !guardedAOTSpecialization));
  if (failed(directFragments))
    return failure();
  markTiming("direct fragment materialization");

  return declareNativeImports(module);
}

LogicalResult NativePipelineAnalysis::materialize() {
  if (bytecodeOnly)
    return success();
  if (!analyses.empty())
    declareProcessSpawnRuntimeABI(module);
  SymbolTable helperSymbols(module);
  for (auto &entry : analyses) {
    auto function = cast<sim::SimFuncOp>(entry.first);
    if (failed(makeProcessActivationHelper(module, helperSymbols, function,
                                           *entry.second)) ||
        failed(makeProcessSpawnHelper(module, helperSymbols, function,
                                      *entry.second,
                                      processSchedules[entry.first])))
      return failure();
  }
  if (useAOT) {
    if (evalScheduler) {
      FailureOr<bool> evalPlan = makeNativeEvalPlan(
          module, dataLayout, aotEligibility.getActorSlots().size(),
          executableNodes, *resolvedEval, *stateLayout, staticNBAPlan,
          staticFanoutPlan, staticActorRoots, *directFragments, evalOwnership,
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
  if (failed(makeSchedulerMain(module, *stateLayout, useAOT, evalScheduler,
                               hasLanguageObserver)))
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
  for (PreparedOrdinaryNativeFunction &function : ordinaryFunctions)
    detail::materializeNativeSpawnBatches(function.body);
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
  SmallVector<schedule::NativeExecuteOp> directCalls;
  module.walk(
      [&](schedule::NativeExecuteOp call) { directCalls.push_back(call); });
  SymbolTableCollection directCallSymbols;
  for (auto call : directCalls) {
    auto callee = directCallSymbols.lookupNearestSymbolFrom<func::FuncOp>(
        call, call.getCalleeAttr());
    if (!callee)
      return call.emitError("scheduled body has no prepared native function");
    TypeRange results = callee.getFunctionType().getResults();
    bool changedStatus = call.getResultTypes() != results;
    if (changedStatus && (call.getNumResults() != 0 || results.size() != 1 ||
                          results.front() != IntegerType::get(context, 32)))
      return call.emitError("scheduled body has an unsupported status ABI");
    OpBuilder builder(call);
    auto replacement =
        func::CallOp::create(builder, call.getLoc(), callee.getSymName(),
                             results, call.getOperands());
    for (NamedAttribute attribute : call->getDiscardableAttrs())
      if (attribute.getName() != call.getCalleeAttrName() &&
          attribute.getName() != call.getStatusResultAttrName())
        replacement->setAttr(attribute.getName(), attribute.getValue());
    if (changedStatus) {
      LLVM::ReturnOp returnOp;
      auto wrapper = call->getParentOfType<LLVM::LLVMFuncOp>();
      if (wrapper)
        wrapper.walk([&](LLVM::ReturnOp candidate) { returnOp = candidate; });
      if (!returnOp || returnOp.getNumOperands() != 1)
        return call.emitError("scheduled wrapper has no status return");
      returnOp->setOperand(0, replacement.getResult(0));
    } else {
      call->replaceAllUsesWith(replacement.getResults());
    }
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
    FailureOr<PreparedSuspendableProcess> prepared = prepareSuspendableProcess(
        function, *analysis, copyActivations.contains(function),
        tableProcesses.contains(function)
            ? std::optional<detail::NativeTableProcess>(
                  tableProcesses.lookup(function))
            : std::nullopt);
    if (failed(prepared))
      return failure();
    suspendableProcesses.push_back(std::move(*prepared));
  }
  markTiming("process body preparation");
  if (detailedTiming)
    llvm::errs() << "obelisk native table processes: "
                 << llvm::count_if(suspendableProcesses,
                                   [](const auto &process) {
                                     return process.tableProcess.has_value();
                                   })
                 << '\n';
  // Batches introduce module-level capture globals. Create them before the
  // parallel workers, which may only mutate their own process bodies.
  for (PreparedPlainNativeProcess &process : plainProcesses)
    detail::materializeNativeSpawnBatches(process.body);
  for (PreparedSuspendableProcess &process : suspendableProcesses)
    detail::materializeNativeSpawnBatches(process.ramp);
  bool needsCoroutine =
      llvm::any_of(suspendableProcesses, [](const auto &process) {
        return !process.directActivation;
      });
  if ((!plainProcesses.empty() || !suspendableProcesses.empty()) &&
      failed(detail::materializeSharedNativeWrappers(module, needsCoroutine)))
    return failure();
  markTiming("shared process support materialization");
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
  detail::materializeCopyKernels(module, suspendableProcesses);
  if (detailedTiming) {
    llvm::DenseSet<Operation *> kernels;
    unsigned copies = 0;
    for (auto &process : suspendableProcesses)
      if (process.copyKernel.kernel) {
        kernels.insert(process.copyKernel.kernel);
        ++copies;
      }
    llvm::errs() << "obelisk shared copy kernels: " << kernels.size() << " for "
                 << copies << " activations\n";
  }
  markTiming("copy kernel materialization");
  // Finalization queries only embedded-design symbols: execution/bytecode
  // entries and the descriptor declarations each process replaces. Their
  // identities are already frozen; later wrappers and frame descriptors
  // introduce different symbols, so all processes share this snapshot and
  // erase replaced declarations through it.
  SymbolTable embeddedSymbols(module);
  for (PreparedPlainNativeProcess &process : plainProcesses)
    if (failed(finishPreparedPlainNativeProcess(process, embeddedSymbols)))
      return failure();
  markTiming("plain process finalization");
  if (llvm::any_of(suspendableProcesses,
                   [](const PreparedSuspendableProcess &process) {
                     return process.tableProcess.has_value();
                   }))
    declareTableProcessRuntimeABI(module);
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
      if (auto function = dyn_cast<LLVM::LLVMFuncOp>(operation)) {
        if (function.isExternal() &&
            module.lookupSymbol(function.getSymName())) {
          function.erase();
          continue;
        }
      } else if (auto global = dyn_cast<LLVM::GlobalOp>(operation)) {
        if (global.getInitializerRegion().empty() && !global.getValue() &&
            module.lookupSymbol(global.getSymName())) {
          global.erase();
          continue;
        }
      }
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
              sim::SimClassFieldDeclOp, schedule::NativeMethodOp,
              sim::SimRandomConstraintTemplateOp>(operation)) {
        operation->erase();
        continue;
      }
      operation->moveBefore(design);
    }
    design.erase();
  }

  SmallVector<schedule::NativeTimeOp> times;
  module.walk([&](schedule::NativeTimeOp op) { times.push_back(op); });
  for (auto time : times) {
    if (time.getTime().getType() != time.getResult().getType())
      return time.emitError("native time was not converted");
    time.getResult().replaceAllUsesWith(time.getTime());
    time.erase();
  }
  SmallVector<schedule::NativeContextOp> projections;
  module.walk([&](schedule::NativeContextOp op) { projections.push_back(op); });
  for (auto projection : projections) {
    if (projection.getContext().getType() != projection.getResult().getType())
      return projection.emitError("native function context was not converted");
    projection.getResult().replaceAllUsesWith(projection.getContext());
    projection.erase();
  }
  return success();
}

} // namespace detail
namespace {
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
    LLVM::LLVMFuncOp variantDispatcher;
    DenseI64ArrayAttr ranges;
    bool independentEntry = false;
    std::optional<uint32_t> checkpointActor;
    std::optional<uint32_t> checkpointContinuation;
    std::string selectorName;
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
         {},
         ranges,
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::EvalConditionallyTwoState>(function),
         checkpointActor,
         checkpointContinuation,
         (Twine("__obelisk_eval_selected_variant_v1_") + Twine(routeIndex))
             .str(),
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
      auto bodyCall =
          LLVM::CallOp::create(builder, route.fourState.getLoc(),
                               route.checkpointBody, callbackArguments);
      // IEEE 1800-2023 4.5/4.6, 10.4.2: execute the shared body once after
      // recording four-state NBA provenance and before resuming the slot.
      bodyCall.setNoInline(true);
      Value bodyStatus = bodyCall.getResult();
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
        if (fourStateFallback)
          call.setNoInline(true);
        LLVM::ReturnOp::create(builder, route.twoState.getLoc(),
                               call.getResults());
      };
      emitTailCall(twoState, route.twoState, false);
      emitTailCall(fourState, route.fourState, true);
    } else {
      // Route selection must report a four-state leaf to the shared
      // NBA barrier. The cold branch calls a wrapper that records the
      // fallback before entering the model body; the promoted branch calls
      // the two-state body directly, so the hot edge stays clean.
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
      // IEEE 1800-2023 4.5/4.6, 10.4.2: keep provenance before this body's
      // publications and NBA staging on every entry through the route.
      call.setNoInline(true);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             call.getResults());
    }
    if (!route.pathKnownProbe) {
      builder.setInsertionPointToStart(module.getBody());
      auto global = LLVM::GlobalOp::create(
          builder, route.twoState.getLoc(), i8, false, LLVM::Linkage::Internal,
          route.selectorName, builder.getI8IntegerAttr(0), 1);
      // IEEE 1800-2023 6.3.1, 6.8: select two-state execution only after its
      // exact proof succeeds; actual X/Z writes revoke that selection.
      if (!route.ranges.empty())
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalRouteProofDependencies>(
            global,
            schedule::RouteProofDependencyAttr::get(
                context, route.ranges, builder.getI64IntegerAttr(routeIndex)));

      builder.setInsertionPointToEnd(module.getBody());
      route.variantDispatcher = LLVM::LLVMFuncOp::create(
          builder, route.twoState.getLoc(),
          (Twine("__obelisk_eval_variant_dispatch_v1_") + Twine(routeIndex))
              .str(),
          route.twoState.getFunctionType());
      detail::copyNativePartition(route.twoState, route.variantDispatcher);
      route.variantDispatcher.setPrivate();
      route.variantDispatcher.setLinkage(LLVM::Linkage::Internal);
      route.variantDispatcher.setAlwaysInline(true);
      Block *entry = route.variantDispatcher.addEntryBlock(builder);
      Block *twoState = new Block, *fourState = new Block;
      route.variantDispatcher.getBody().push_back(twoState);
      route.variantDispatcher.getBody().push_back(fourState);
      builder.setInsertionPointToStart(entry);
      Value selected = LLVM::LoadOp::create(
          builder, route.twoState.getLoc(), i8,
          LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                    route.selectorName),
          1);
      Value known = LLVM::ICmpOp::create(
          builder, route.twoState.getLoc(), LLVM::ICmpPredicate::ne, selected,
          detail::llvmConstant(builder, route.twoState.getLoc(), i8, 0));
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), known, twoState,
                             fourState);
      auto emitCall = [&](Block *block, LLVM::LLVMFuncOp callee) {
        builder.setInsertionPointToStart(block);
        auto call = LLVM::CallOp::create(builder, route.twoState.getLoc(),
                                         callee, entry->getArguments());
        LLVM::ReturnOp::create(builder, route.twoState.getLoc(),
                               call.getResults());
      };
      emitCall(twoState, route.twoState);
      emitCall(fourState, route.fourStateFallback);
    }

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
      Value selected = LLVM::ZExtOp::create(builder, location, i8, known);
      LLVM::StoreOp::create(builder, location, selected,
                            LLVM::AddressOfOp::create(
                                builder, location, pointer, route.selectorName),
                            1);
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
        if (route.pathKnownProbe)
          continue;
        LLVM::StoreOp::create(
            builder, returnOp.getLoc(),
            detail::llvmConstant(builder, returnOp.getLoc(), i8, 0),
            LLVM::AddressOfOp::create(builder, returnOp.getLoc(), pointer,
                                      route.selectorName),
            1);
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

  DialectRegistry registry;
  LLVM::registerInlinerInterface(registry);
  context->appendDialectRegistry(registry);
  InlinerInterface inliner(context);
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
        (caller == route.dispatcher || caller == route.fourStateFallback ||
         caller == route.variantDispatcher))
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
    call.setCallee(route.variantDispatcher.getSymName());
    // IEEE 1800-2023 4.5/4.6: keep selection at this execution boundary.
    // Expand only the branch before partitioning can separate its callees.
    auto clone = [&](OpBuilder &, Region *source, Block *inlineBlock,
                     Block *postInsertBlock, IRMapping &mapping,
                     bool shouldClone) {
      assert(shouldClone && "variant selectors must remain reusable");
      source->cloneInto(inlineBlock->getParent(),
                        postInsertBlock->getIterator(), mapping);
    };
    if (failed(inlineCall(inliner, clone, cast<CallOpInterface>(*call),
                          cast<CallableOpInterface>(*route.variantDispatcher),
                          &route.variantDispatcher.getBody())))
      return call.emitError("could not expand direct variant selection");
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

  auto fastClone = module.lookupSymbol<LLVM::LLVMFuncOp>(fastTwoStateName);
  if (!fastClone) {
    builder.setInsertionPointAfter(clone);
    fastClone = cast<LLVM::LLVMFuncOp>(builder.clone(*clone.getOperation()));
    fastClone.setSymName(fastTwoStateName);
  }
  SmallVector<LLVM::LoadOp> stagedUnknownLoads;
  for (auto variant : {clone, fastClone})
    variant.walk([&](Operation *operation) {
      if (auto load = dyn_cast<LLVM::LoadOp>(operation);
          load &&
          ::obelisk::schedule::has<
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

class ConvertPreparedSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertPreparedSimProcessesToLLVMCoroutinesPassBase<
          ConvertPreparedSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto cached = getCachedAnalysis<detail::NativePipelineAnalysis>();
    if (!cached || cached->get().stage !=
                       detail::NativePipelineAnalysis::Stage::Materialized) {
      module.emitError(
          "LLVM coroutine conversion requires the native preparation pipeline");
      return signalPassFailure();
    }
    // Nested finalization pipelines invalidate the outer analysis cache.
    // Own the target facts and timing state needed by this terminal pass.
    llvm::DataLayout targetLayout = cached->get().dataLayout;
    const llvm::DataLayout *parsed = &targetLayout;
    bool detailedTiming = cached->get().detailedTiming;
    auto lastTiming = std::chrono::steady_clock::now();
    auto markTiming = [&](StringRef name) {
      if (!detailedTiming)
        return;
      auto now = std::chrono::steady_clock::now();
      llvm::errs() << "obelisk native preparation timing: " << name << ": "
                   << std::chrono::duration<double>(now - lastTiming).count()
                   << " s\n";
      lastTiming = now;
    };
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
      runtimeTarget.addIllegalOp<schedule::NativeScratchOp>();
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
    SmallVector<schedule::NativeObserverOp> deadObservers;
    module.walk([&](schedule::NativeObserverOp observer) {
      if (observer->use_empty())
        deadObservers.push_back(observer);
    });
    for (auto observer : deadObservers)
      observer.erase();
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

namespace obelisk {
#define GEN_PASS_DEF_PREPARENATIVESCHEDULEINPUTSPASS
#define GEN_PASS_DEF_PREPARENATIVEPROCESSFRAMESPASS
#define GEN_PASS_DEF_PREPARENATIVEMANAGEDROOTSPASS
#define GEN_PASS_DEF_PREPARENATIVEFRAGMENTSPASS
#define GEN_PASS_DEF_MATERIALIZENATIVEPROCESSESPASS
#include "obelisk/Conversion/Passes.h.inc"
namespace {
class PrepareNativeScheduleInputsPass final
    : public impl::PrepareNativeScheduleInputsPassBase<
          PrepareNativeScheduleInputsPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto &state = getAnalysis<Analysis>();
    if (state.stage != Analysis::Stage::Empty) {
      getOperation().emitError("prepare-native-schedule-inputs requires native "
                               "pipeline phase Empty");
      return signalPassFailure();
    }
    if (failed(state.initialize()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Inputs;
    markAnalysesPreserved<Analysis>();
  }
};
class PrepareNativeProcessFramesPass final
    : public impl::PrepareNativeProcessFramesPassBase<
          PrepareNativeProcessFramesPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("prepare-native-process-frames requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::State) {
      getOperation().emitError(
          "prepare-native-process-frames requires native pipeline phase State");
      return signalPassFailure();
    }
    if (failed(state.prepareFrames()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Frames;
    markAnalysesPreserved<Analysis>();
  }
};
class PrepareNativeManagedRootsPass final
    : public impl::PrepareNativeManagedRootsPassBase<
          PrepareNativeManagedRootsPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("prepare-native-managed-roots requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Schedule) {
      getOperation().emitError("prepare-native-managed-roots requires native "
                               "pipeline phase Schedule");
      return signalPassFailure();
    }
    if (failed(state.prepareRoots()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Roots;
    markAnalysesPreserved<Analysis>();
  }
};
class PrepareNativeFragmentsPass final
    : public impl::PrepareNativeFragmentsPassBase<PrepareNativeFragmentsPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("prepare-native-fragments requires the native "
                               "preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::EvalVariants) {
      getOperation().emitError("prepare-native-fragments requires native "
                               "pipeline phase EvalVariants");
      return signalPassFailure();
    }
    if (failed(state.prepareFragments()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Fragments;
    markAnalysesPreserved<Analysis>();
  }
};
class MaterializeNativeProcessesPass final
    : public impl::MaterializeNativeProcessesPassBase<
          MaterializeNativeProcessesPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("materialize-native-processes requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Resolved) {
      getOperation().emitError("materialize-native-processes requires native "
                               "pipeline phase Resolved");
      return signalPassFailure();
    }
    if (failed(state.materialize()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Materialized;
    markAnalysesPreserved<Analysis>();
  }
};
} // namespace
} // namespace obelisk
