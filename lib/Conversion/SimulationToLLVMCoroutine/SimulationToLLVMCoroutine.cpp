//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

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
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MathToLLVM/MathToLLVM.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

#include <algorithm>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

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
using detail::insertAutomaticOwnerReleases;
using detail::instrumentManagedRoots;
using detail::lowerOrdinaryFunction;
using detail::lowerPackedSimulationOperations;
using detail::lowerPlainNativeProcess;
using detail::lowerSuspendableProcess;
using detail::makeDirectFragmentWrapper;
using detail::makeNativeAOTPlanLegacy;
using detail::makeNativeEvalPlan;
using detail::makeProcessActivationHelper;
using detail::makeProcessSpawnHelper;
using detail::makeSchedulerMain;
using detail::makeStatePlane;
using detail::markCleanStaticNBAsInGuardedBodies;
using detail::materializeGeneratedNBAAccumulators;
using detail::materializeManagedMethodThunks;
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
using detail::prepareManagedLowering;
using detail::specializeNativeAOTCaptures;
using detail::threadProcessStateThroughCFG;
using detail::validateProcessABI;

/// Preserve the compact-NBA conversion proof on the operation that consumes
/// it. Function and NBA conversion patterns may run in either order, so the
/// NBA lowering must not depend on its parent function still being present.
static void annotateCompactNBAMetadata(ModuleOp module) {
  MLIRContext *context = module.getContext();
  module.walk([&](sim::SimFuncOp function) {
    if (!function->hasAttr("obelisk.eval.selected_two_state"))
      return;
    function.walk([&](sim::SimNBAEnqueueOp nba) {
      nba->setAttr(sim::metadata::evalCompactNBAMetadata,
                   UnitAttr::get(context));
    });
  });
}

LogicalResult materializeEvalTwoStateVariants(
    ModuleOp module, sim::SimDesignOp design,
    const detail::NativeStateLayout &stateLayout, bool enabled) {
  if (!enabled || !design)
    return success();
  // Selecting the Eval architecture does not change the language's state
  // domain. Two-state variants still require the same inductive closure proof
  // as Auto and are selected only after their canonical unknown plane clears.
  constexpr bool forceTwoState = false;
  FailureOr<StateDomainAnalysis> domains =
      StateDomainAnalysis::compute(design, /*proveInductiveRoots=*/true);
  if (failed(domains))
    return failure();
  FailureOr<StateDomainAnalysis> knownStateDomains =
      StateDomainAnalysis::computeAssumingKnownState(design);
  if (failed(knownStateDomains))
    return failure();

  SmallVector<sim::SimFuncOp> roots;
  llvm::SmallPtrSet<Operation *, 16> rootSet;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    auto body = function->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body");
    if (!body)
      continue;
    sim::SimFuncOp target =
        design.lookupSymbol<sim::SimFuncOp>(body.getValue());
    if (!target || !target->hasAttr("obelisk.eval.raw_captures") ||
        target->hasAttr("obelisk.eval.inductive_two_state"))
      continue;
    if (rootSet.insert(target.getOperation()).second)
      roots.push_back(target);
  }

  SmallVector<sim::SimFuncOp> sources;
  llvm::SmallPtrSet<Operation *, 32> sourceSet;
  if (forceTwoState)
    for (sim::SimFuncOp root : roots)
      if (sourceSet.insert(root.getOperation()).second)
        sources.push_back(root);
  for (sim::SimFuncOp root : roots) {
    if (forceTwoState)
      continue;
    SmallVector<sim::SimFuncOp> closure{root};
    llvm::SmallPtrSet<Operation *, 16> closureSet;
    for (size_t index = 0; index != closure.size(); ++index) {
      sim::SimFuncOp function = closure[index];
      if (!closureSet.insert(function.getOperation()).second)
        continue;
      function.walk([&](sim::SimCallOp call) {
        sim::SimFuncOp callee =
            design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
        if (!callee || callee.isExternal())
          return;
        closure.push_back(callee);
      });
    }
    for (sim::SimFuncOp function : closure)
      if (sourceSet.insert(function.getOperation()).second)
        sources.push_back(function);
  }

  if (sources.empty())
    return success();
  llvm::SmallPtrSet<Operation *, 32> variantEligibleSources;
  if (!forceTwoState) {
    // A transient two-state route need not be globally two-state.  It is
    // sufficient that (1) every canonical input/output slice is known at the
    // quiescent handoff and (2) the complete instance body is known-input
    // preserving.  Asynchronous writes invalidate all routes before another
    // generated activation.  This is the generalized form of the useful
    // startup behavior: four-state work reaches a safe boundary once, while
    // the steady-state instance body does not carry unknown-plane traffic.
    using PhysicalRange = std::pair<uint64_t, uint64_t>;
    llvm::SmallDenseSet<PhysicalRange, 32> selectedRanges;
    DenseMap<Operation *, SmallVector<PhysicalRange>> localRangeMap;
    DenseMap<Operation *, SmallVector<PhysicalRange>> inductiveRangeMap;
    DenseMap<Operation *, bool> locallyKnownPreserving;
    DenseMap<Operation *, bool> locallyRuntimeFree;
    DenseMap<Operation *, bool> locallyCheckpointSafe;
    llvm::SmallPtrSet<Operation *, 16> routeEligibleSources;
    bool invalidRange = false;
    auto selectRange =
        [&](Value handle, const analysis::DescriptorProvenanceMap &provenance,
            llvm::SmallDenseSet<PhysicalRange, 8> &localRanges) -> bool {
      auto found = provenance.find(handle);
      if (found == provenance.end() || !found->second.descriptor ||
          found->second.dynamic)
        return false;
      uint64_t width = found->second.width != 0 ? found->second.width
                                                : found->second.rootWidth;
      if (width == 0 || found->second.low > found->second.rootWidth ||
          width > found->second.rootWidth - found->second.low)
        return false;
      auto insertRange = [&](uint64_t offset, uint64_t rangeWidth) {
        PhysicalRange range{offset, rangeWidth};
        localRanges.insert(range);
      };
      const auto *handles =
          found->second.resource == sim::ComputeResourceKind::Storage
              ? &stateLayout.storage
          : found->second.resource == sim::ComputeResourceKind::Net
              ? &stateLayout.nets
              : nullptr;
      if (!handles)
        return false;
      auto handleValue = handles->find(*found->second.descriptor);
      obelisk_rt_stable_handle_v1 decoded{};
      if (handleValue == handles->end() ||
          !obelisk_rt_stable_handle_decode(handleValue->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0) {
        invalidRange = true;
        return false;
      }
      auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &entry) {
        return entry.handleID == decoded.id;
      });
      if (bound == stateLayout.bounds.end() ||
          found->second.low > bound->width ||
          width > bound->width - found->second.low) {
        invalidRange = true;
        return false;
      }
      if (!bound->fourState)
        return true;
      insertRange(bound->offset + found->second.low, width);
      return true;
    };
    for (sim::SimFuncOp source : sources) {
      analysis::DescriptorProvenanceMap provenance =
          analysis::deriveDescriptorProvenance(source);
      llvm::SmallDenseSet<PhysicalRange, 8> localRanges;
      llvm::SmallDenseSet<PhysicalRange, 8> inductiveRanges;
      bool preserving = true;
      bool runtimeFree = true;
      bool checkpointSafe = true;
      // A checkpoint probe intercepts the activation before the original body
      // executes any operation in a block containing an unconditional cold
      // exit. State touched solely in that block belongs to the Tier-3
      // transaction and must not pollute the generated path's promotion
      // closure. Branch predicates leading to the block remain in their
      // predecessor and are still analyzed normally.
      llvm::SmallPtrSet<Block *, 4> coldCheckpointBlocks;
      source.walk([&](Operation *operation) {
        if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
                sim::SimTerminationRequestedOp, sim::SimStatusCheckOp,
                sim::SimDisplayOp>(operation))
          coldCheckpointBlocks.insert(operation->getBlock());
      });
      source.walk([&](Operation *operation) {
        if (operation->getName().getDialectNamespace() == "obelisk_rt") {
          preserving = false;
          runtimeFree = false;
          checkpointSafe = false;
          return;
        }
        if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
                sim::SimTerminationRequestedOp, sim::SimStatusCheckOp,
                sim::SimDisplayOp>(operation)) {
          // These operations are cold checkpoint exits.  They do not create
          // or consume persistent four-state data in the generated body, so
          // the surrounding module-instance logic can still have a two-state
          // variant.  The coordinator retains the status/termination edge and
          // executes the runtime call only when the branch is taken.
          runtimeFree = false;
          return;
        }
        if (isa<sim::SimFileOpenMCDOp, sim::SimFileOpenOp, sim::SimFileCloseOp,
                sim::SimFileFlushOp, sim::SimFileGetcOp, sim::SimFileUngetcOp,
                sim::SimFileGetlineOp, sim::SimFileReadPackedOp,
                sim::SimFileEofOp, sim::SimFileSeekOp, sim::SimFileTellOp,
                sim::SimFileRewindOp>(operation)) {
          preserving = false;
          runtimeFree = false;
          checkpointSafe = false;
          return;
        }
        if (coldCheckpointBlocks.contains(operation->getBlock()))
          return;
        if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
          if (domains->isTwoStateWithInductiveRoots(load.getResult())) {
            auto found = provenance.find(load.getReference());
            if (found != provenance.end() && found->second.descriptor &&
                domains->isInductivelyTwoState(found->second.resource,
                                               *found->second.descriptor))
              (void)selectRange(load.getReference(), provenance,
                                inductiveRanges);
          }
          preserving &=
              knownStateDomains->isTwoStateWithInductiveRoots(
                  load.getResult()) &&
              selectRange(load.getReference(), provenance, localRanges);
          return;
        }
        if (auto read = dyn_cast<sim::SimNetReadOp>(operation)) {
          if (domains->isTwoStateWithInductiveRoots(read.getResult())) {
            auto found = provenance.find(read.getNet());
            if (found != provenance.end() && found->second.descriptor &&
                domains->isInductivelyTwoState(found->second.resource,
                                               *found->second.descriptor))
              (void)selectRange(read.getNet(), provenance, inductiveRanges);
          }
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            read.getResult()) &&
                        selectRange(read.getNet(), provenance, localRanges);
          return;
        }
        if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
          auto found = provenance.find(store.getReference());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                             *found->second.descriptor))
            (void)selectRange(store.getReference(), provenance,
                              inductiveRanges);
          preserving &=
              knownStateDomains->isTwoStateWithInductiveRoots(
                  store.getValue()) &&
              selectRange(store.getReference(), provenance, localRanges);
          return;
        }
        if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          auto found = provenance.find(nba.getDestination());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                             *found->second.descriptor))
            (void)selectRange(nba.getDestination(), provenance,
                              inductiveRanges);
          preserving &=
              knownStateDomains->isTwoStateWithInductiveRoots(nba.getValue()) &&
              selectRange(nba.getDestination(), provenance, localRanges);
          return;
        }
        if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
          auto found = provenance.find(drive.getDriver());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                             *found->second.descriptor))
            (void)selectRange(drive.getDriver(), provenance, inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            drive.getValue()) &&
                        selectRange(drive.getDriver(), provenance, localRanges);
          return;
        }
        if (auto drive = dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
          auto found = provenance.find(drive.getDriver());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                             *found->second.descriptor))
            (void)selectRange(drive.getDriver(), provenance, inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            drive.getValue()) &&
                        selectRange(drive.getDriver(), provenance, localRanges);
          return;
        }
        if (auto branch = dyn_cast<cf::CondBranchOp>(operation))
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
              branch.getCondition());
        if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
          sim::SimFuncOp callee =
              design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          preserving &= callee && !callee.isExternal();
        }
      });
      SmallVector<PhysicalRange> orderedRanges(localRanges.begin(),
                                               localRanges.end());
      llvm::sort(orderedRanges);
      localRangeMap[source.getOperation()] = std::move(orderedRanges);
      SmallVector<PhysicalRange> orderedInductiveRanges(inductiveRanges.begin(),
                                                        inductiveRanges.end());
      llvm::sort(orderedInductiveRanges);
      inductiveRangeMap[source.getOperation()] =
          std::move(orderedInductiveRanges);
      locallyKnownPreserving[source.getOperation()] = preserving;
      locallyRuntimeFree[source.getOperation()] = runtimeFree;
      locallyCheckpointSafe[source.getOperation()] = checkpointSafe;
    }
    for (sim::SimFuncOp source : sources) {
      if (!source->hasAttr("obelisk.eval.raw_captures"))
        continue;
      llvm::SmallDenseSet<PhysicalRange, 16> closureRanges;
      llvm::SmallDenseSet<PhysicalRange, 16> inductiveClosureRanges;
      SmallVector<sim::SimFuncOp> closure{source};
      llvm::SmallPtrSet<Operation *, 16> seen;
      bool closureKnownPreserving = true;
      bool closureRuntimeFree = true;
      bool closureCheckpointSafe = true;
      for (size_t index = 0; index != closure.size(); ++index) {
        sim::SimFuncOp function = closure[index];
        if (!seen.insert(function.getOperation()).second)
          continue;
        closureKnownPreserving &=
            locallyKnownPreserving.lookup(function.getOperation());
        closureRuntimeFree &=
            locallyRuntimeFree.lookup(function.getOperation());
        closureCheckpointSafe &=
            locallyCheckpointSafe.lookup(function.getOperation());
        for (PhysicalRange range : localRangeMap[function.getOperation()])
          closureRanges.insert(range);
        for (PhysicalRange range : inductiveRangeMap[function.getOperation()])
          inductiveClosureRanges.insert(range);
        function.walk([&](sim::SimCallOp call) {
          sim::SimFuncOp callee =
              design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          if (!callee || callee.isExternal())
            return;
          // Raw-capture eval bodies retain independently selected routes.
          // Ordinary value helpers remain in this body's boundary because
          // their direct logic results cannot be routed independently.
          if (callee != source && callee->hasAttr("obelisk.eval.raw_captures"))
            return;
          closure.push_back(callee);
        });
      }
      for (BlockArgument argument : source.getBody().front().getArguments())
        if (detail::containsLogic(argument.getType()))
          closureKnownPreserving &= domains->isTwoState(argument);
      if (!closureRuntimeFree && !closureCheckpointSafe)
        continue;
      if (closureRuntimeFree)
        for (PhysicalRange range : inductiveClosureRanges)
          selectedRanges.insert(range);
      if (!closureKnownPreserving)
        closureRanges.clear();
      if (!closureKnownPreserving)
        closureRanges = std::move(inductiveClosureRanges);
      SmallVector<PhysicalRange> orderedRanges(closureRanges.begin(),
                                               closureRanges.end());
      llvm::sort(orderedRanges);
      // A checkpoint-safe body can still contribute a whole-owner promotion
      // proof when all of its generated paths preserve known state. Keep the
      // union of its persistent ranges for that proof; the runtime leaf is
      // fractured before generated execution and is not part of the state
      // closure. Owners without this inductive fact retain path-local probes.
      if (!closureRuntimeFree && !closureKnownPreserving)
        orderedRanges.clear();
      SmallVector<int64_t> encoded;
      for (auto [offset, width] : orderedRanges) {
        encoded.push_back(static_cast<int64_t>(offset));
        encoded.push_back(static_cast<int64_t>(width));
      }
      source->setAttr("obelisk.eval.local_promotion_ranges",
                      DenseI64ArrayAttr::get(module.getContext(), encoded));
      if (closureKnownPreserving) {
        source->setAttr("obelisk.eval.conditionally_two_state",
                        UnitAttr::get(module.getContext()));
      }
      if (!closureRuntimeFree)
        source->setAttr("obelisk.eval.inherited_two_state_checkpoint",
                        UnitAttr::get(module.getContext()));
      if (!closureRuntimeFree && closureKnownPreserving)
        source->setAttr(sim::metadata::evalPathGuardedKnownPreserving,
                        UnitAttr::get(module.getContext()));
      if (closureKnownPreserving || !orderedRanges.empty() ||
          !closureRuntimeFree)
        routeEligibleSources.insert(source.getOperation());
    }
    // Do not build a nominal two-state owner that can retain a permanently
    // four-state raw-capture leaf.  Such a leaf could stage X/Z into the shared
    // NBA accumulator while the promoted coordinator selects its two-state
    // commit.  Reject these closures transitively instead of relying on a
    // top-level fallback bit that cannot see the indirect route.
    bool removed;
    do {
      removed = false;
      SmallVector<Operation *> rejected;
      for (Operation *operation : routeEligibleSources) {
        auto source = cast<sim::SimFuncOp>(operation);
        bool closed = true;
        source.walk([&](sim::SimCallOp call) {
          sim::SimFuncOp callee =
              design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          if (callee && callee->hasAttr("obelisk.eval.raw_captures") &&
              !routeEligibleSources.contains(callee.getOperation()))
            closed = false;
        });
        if (!closed)
          rejected.push_back(operation);
      }
      for (Operation *operation : rejected) {
        removed |= routeEligibleSources.erase(operation);
        operation->removeAttr("obelisk.eval.conditionally_two_state");
      }
    } while (removed);

    // A generated owner may call another raw-capture owner (for example, a
    // parent module instance calling an outlined child instance).  Its entry
    // boundary must cover the complete selected call closure: once the outer
    // wrapper takes its two-state edge, those calls are rewritten directly to
    // their two-state variants and cannot perform a second boundary check.
    DenseMap<Operation *, llvm::SmallDenseSet<PhysicalRange, 16>>
        routeClosureRanges;
    for (Operation *operation : routeEligibleSources) {
      auto source = cast<sim::SimFuncOp>(operation);
      auto encoded = source->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.eval.local_promotion_ranges");
      if (!encoded || (encoded.size() & 1) != 0)
        return source.emitError("has malformed local promotion ranges"),
               failure();
      ArrayRef<int64_t> values = encoded.asArrayRef();
      for (size_t index = 0; index != values.size(); index += 2)
        routeClosureRanges[operation].insert(
            {static_cast<uint64_t>(values[index]),
             static_cast<uint64_t>(values[index + 1])});
    }
    do {
      removed = false;
      for (Operation *operation : routeEligibleSources) {
        auto source = cast<sim::SimFuncOp>(operation);
        source.walk([&](sim::SimCallOp call) {
          sim::SimFuncOp callee =
              design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          if (!callee || !routeEligibleSources.contains(callee.getOperation()))
            return;
          // A checkpoint callee owns its own guarded route and promotion
          // closure. Pulling its dormant-path ranges into every caller would
          // couple otherwise independent module instances and prevent the
          // caller from ever reaching its two-state entry.
          if (callee->hasAttr("obelisk.eval.inherited_two_state_checkpoint"))
            return;
          for (PhysicalRange range : routeClosureRanges[callee.getOperation()])
            removed |= routeClosureRanges[operation].insert(range).second;
        });
      }
    } while (removed);
    for (Operation *operation : routeEligibleSources) {
      SmallVector<PhysicalRange> orderedRanges(
          routeClosureRanges[operation].begin(),
          routeClosureRanges[operation].end());
      llvm::sort(orderedRanges);
      SmallVector<int64_t> encoded;
      for (auto [offset, width] : orderedRanges) {
        encoded.push_back(static_cast<int64_t>(offset));
        encoded.push_back(static_cast<int64_t>(width));
        if (!operation->hasAttr("obelisk.eval.inherited_two_state_checkpoint"))
          selectedRanges.insert({offset, width});
      }
      operation->setAttr("obelisk.eval.local_promotion_ranges",
                         DenseI64ArrayAttr::get(module.getContext(), encoded));
    }

    SmallVector<sim::SimFuncOp> eligibleClosure;
    for (Operation *operation : routeEligibleSources)
      eligibleClosure.push_back(cast<sim::SimFuncOp>(operation));
    for (size_t index = 0; index != eligibleClosure.size(); ++index) {
      sim::SimFuncOp function = eligibleClosure[index];
      if (!variantEligibleSources.insert(function.getOperation()).second)
        continue;
      function.walk([&](sim::SimCallOp call) {
        sim::SimFuncOp callee =
            design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
        if (!callee || callee.isExternal())
          return;
        if (callee->hasAttr("obelisk.eval.raw_captures") &&
            !routeEligibleSources.contains(callee.getOperation()))
          return;
        eligibleClosure.push_back(callee);
      });
    }
    if (invalidRange)
      return design.emitOpError(
          "cannot map an eval promotion slice to native state");
  }

  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  llvm::SmallDenseSet<uint64_t, 32> usedCodeUnits;
  llvm::DenseMap<uint64_t, uint64_t> codeUnitScopes;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
    usedCodeUnits.insert(declaration.getId());
    codeUnitScopes.try_emplace(declaration.getId(), declaration.getScopeId());
  }
  uint64_t nextCodeUnit = 1;
  llvm::StringMap<std::string> variantNames;
  SmallVector<sim::SimFuncOp> variants;
  DenseMap<Operation *, Operation *> variantDeclarations;
  llvm::StringSet<> unsupportedVariantSources;
  SmallVector<Attribute> pathProbeRoutes;
  auto allocateCodeUnit = [&] {
    while (usedCodeUnits.contains(nextCodeUnit))
      ++nextCodeUnit;
    usedCodeUnits.insert(nextCodeUnit);
    return nextCodeUnit++;
  };

  // Build a side-effect-free entry predicate while the Simulation CFG still
  // preserves source short-circuiting.  A whole-owner range scan is necessarily
  // path insensitive: an X input on an untaken branch would otherwise keep the
  // owner in its four-state body forever.  The predicate follows the activation
  // CFG and rejects the two-state edge only when a four-state load that is
  // actually reached contains X/Z. Restrict this first implementation to
  // reads, fixed NBA staging, and cold checkpoints. The cold route resumes the
  // same generated slot coordinator, so staged NBA and downstream fixpoint
  // work still reach the one combined barrier exactly once.
  // A net whose handle was not authorized as directly addressable — VPI
  // observability and language overrides both withdraw that authorization —
  // materializes through a runtime plane accessor. A probe reading it would
  // carry that call into the generated closure, so those owners keep their
  // canonical route instead.
  bool netsDirectlyAddressable =
      llvm::all_of(stateLayout.nets, [&](const auto &entry) {
        obelisk_rt_stable_handle_v1 decoded{};
        return obelisk_rt_stable_handle_decode(entry.second, &decoded) &&
               stateLayout.directHandles.contains(decoded.id);
      });
  auto materializePathKnownProbe =
      [&](sim::SimFuncOp source, StringRef name,
          uint64_t codeUnit,
          bool trackKnownState = true) -> FailureOr<sim::SimFuncOp> {
    bool supported = true;
    source.walk([&](Operation *operation) {
      if (!supported)
        return;
      if (operation == source.getOperation())
        return;
      if (!netsDirectlyAddressable && isa<sim::SimNetReadOp>(operation)) {
        supported = false;
        return;
      }
      if (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveChangedOp>(operation)) {
        supported = false;
        return;
      }
      if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimReturnOp,
              sim::SimNBAEnqueueOp, sim::SimDisplayOp, sim::SimFinishOp,
              sim::SimStopOp, sim::SimFatalOp, sim::SimTerminationRequestedOp,
              sim::SimStatusCheckOp, cf::BranchOp, cf::CondBranchOp>(operation))
        return;
      if (isa<sim::SimCallOp>(operation) || !isMemoryEffectFree(operation)) {
        supported = false;
      }
    });
    if (!supported)
      return sim::SimFuncOp{};

    SmallVector<DictionaryAttr> argumentAttrs;
    for (BlockArgument argument : source.getBody().front().getArguments())
      argumentAttrs.push_back(source.getArgAttrDict(argument.getArgNumber()));
    SmallVector<NamedAttribute> attributes{builder.getNamedAttr(
        "code_unit_id", builder.getI64IntegerAttr(codeUnit))};
    builder.setInsertionPointToEnd(&design.getBody().front());
    sim::SimFuncOp probe = sim::SimFuncOp::create(
        builder, source.getLoc(), name,
        FunctionType::get(module.getContext(),
                          source.getFunctionType().getInputs(),
                          TypeRange{builder.getI8Type()}),
        sim::EntryKind::Function, attributes, argumentAttrs);
    probe->setAttr("obelisk.eval.borrowed_captures", builder.getUnitAttr());
    probe->setAttr("obelisk.eval.path_known_predicate", builder.getUnitAttr());
    SymbolTable::setSymbolVisibility(probe, SymbolTable::Visibility::Private);

    IRMapping mapping;
    Block &sourceEntry = source.getBody().front();
    Block &probeEntry = probe.getBody().front();
    mapping.map(&sourceEntry, &probeEntry);
    for (auto [from, to] :
         llvm::zip_equal(sourceEntry.getArguments(), probeEntry.getArguments()))
      mapping.map(from, to);
    for (Block &sourceBlock : llvm::drop_begin(source.getBody())) {
      Block *probeBlock = new Block;
      probe.getBody().push_back(probeBlock);
      mapping.map(&sourceBlock, probeBlock);
      for (BlockArgument argument : sourceBlock.getArguments())
        mapping.map(argument, probeBlock->addArgument(argument.getType(),
                                                      argument.getLoc()));
    }
    for (Block &sourceBlock : source.getBody()) {
      builder.setInsertionPointToEnd(mapping.lookup(&sourceBlock));
      for (Operation &operation : sourceBlock)
        builder.clone(operation, mapping);
    }

    llvm::SmallPtrSet<Block *, 4> checkpointBlocks;
    llvm::MapVector<Block *, Location> checkpoints;
    probe.walk([&](Operation *operation) {
      if (isa<sim::SimDisplayOp, sim::SimFinishOp, sim::SimStopOp,
              sim::SimFatalOp, sim::SimTerminationRequestedOp,
              sim::SimStatusCheckOp>(operation)) {
        Block *block = operation->getBlock();
        checkpoints.try_emplace(block, operation->getLoc());
      }
    });
    for (auto [block, location] : checkpoints) {
      checkpointBlocks.insert(block);
      block->clear();
      builder.setInsertionPointToEnd(block);
      Value checkpoint = arith::ConstantOp::create(
          builder, location, builder.getI8Type(), builder.getI8IntegerAttr(2));
      sim::SimReturnOp::create(builder, location, checkpoint);
    }

    llvm::SmallPtrSet<Block *, 16> reachable;
    SmallVector<Block *> pending{&probe.getBody().front()};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (!reachable.insert(block).second || block->empty())
        continue;
      for (Block *successor : block->getTerminator()->getSuccessors())
        pending.push_back(successor);
    }
    SmallVector<Block *> unreachable;
    for (Block &block : probe.getBody())
      if (!reachable.contains(&block))
        unreachable.push_back(&block);
    for (Block *block : unreachable)
      block->dropAllDefinedValueUses();
    for (Block *block : unreachable)
      block->dropAllReferences();
    for (Block *block : unreachable)
      block->erase();

    // A path-guarded route is only meaningful when some activation stays in
    // the generated closure. If every reachable exit is a checkpoint block,
    // the probe degenerates to the constant checkpoint answer: the dispatcher
    // would replace the whole activation with a bare checkpoint publication,
    // dropping the NBA staging that precedes the cold leaf and the edge
    // qualification that selected the activation. Decline the owner so it
    // keeps its canonical four-state route.
    bool hasGeneratedExit = false;
    probe.walk([&](sim::SimReturnOp returnOp) {
      if (!checkpointBlocks.contains(returnOp->getBlock()))
        hasGeneratedExit = true;
    });
    if (!hasGeneratedExit) {
      probe.erase();
      return sim::SimFuncOp{};
    }

    SmallVector<Operation *> publications;
    probe.walk([&](Operation *operation) {
      if (isa<sim::SimNBAEnqueueOp, sim::SimRefStoreOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveChangedOp>(operation))
        publications.push_back(operation);
    });
    for (Operation *publication : publications)
      publication->erase();

    if (!trackKnownState) {
      SmallVector<sim::SimReturnOp> returns;
      probe.walk([&](sim::SimReturnOp returnOp) {
        if (!checkpointBlocks.contains(returnOp->getBlock()))
          returns.push_back(returnOp);
      });
      for (sim::SimReturnOp returnOp : returns) {
        builder.setInsertionPoint(returnOp);
        Value direct = arith::ConstantOp::create(
            builder, returnOp.getLoc(), builder.getI8Type(),
            builder.getI8IntegerAttr(1));
        sim::SimReturnOp::create(builder, returnOp.getLoc(), direct);
        returnOp.erase();
      }
      return probe;
    }

    SmallVector<Value> loadedValues;
    probe.walk([&](Operation *operation) {
      if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
        loadedValues.push_back(load.getResult());
      else if (auto read = dyn_cast<sim::SimNetReadOp>(operation))
        loadedValues.push_back(read.getResult());
    });

    // The probe must classify the complete four-state path, even after it
    // observes an unknown value. Returning at the first X/Z load conflates a
    // four-state body with a later Tier-3 checkpoint and can execute a native
    // prefix before restarting the runtime body. Thread path-knownness through
    // the CFG as an SSA block argument; introducing a late automatic reference
    // here would bypass the process lifetime analysis.
    builder.setInsertionPointToStart(&probeEntry);
    Value initiallyKnown = arith::ConstantOp::create(
        builder, source.getLoc(), builder.getI1Type(),
        builder.getBoolAttr(true));
    DenseMap<Block *, Value> incomingKnown;
    incomingKnown[&probeEntry] = initiallyKnown;
    for (Block &block : llvm::drop_begin(probe.getBody()))
      incomingKnown[&block] =
          block.addArgument(builder.getI1Type(), source.getLoc());

    llvm::SmallDenseSet<Value, 32> stateLoads(loadedValues.begin(),
                                              loadedValues.end());
    DenseMap<Block *, Value> outgoingKnown;
    for (Block &block : probe.getBody()) {
      Value knownSoFar = incomingKnown.lookup(&block);
      SmallVector<Value> blockLoads;
      for (Operation &operation : block)
        for (Value result : operation.getResults())
          if (stateLoads.contains(result))
            blockLoads.push_back(result);
      for (Value loaded : blockLoads) {
        std::optional<unsigned> width =
            detail::nativeStateWidth(loaded.getType());
        // A load whose type has no native logic width cannot be probed for
        // known state. That is a limit of this predicate, not an invalid
        // design: decline the owner so it keeps its canonical four-state
        // route instead of failing an otherwise legal compilation.
        if (!width || !detail::containsLogic(loaded.getType())) {
          probe.erase();
          return sim::SimFuncOp{};
        }
        Operation *load = loaded.getDefiningOp();
        builder.setInsertionPointAfter(load);
        auto logicType = sim::LogicType::get(module.getContext(), *width);
        Value flattened = loaded;
        if (loaded.getType() != logicType)
          flattened = sim::SimPackedFlattenOp::create(
              builder, load->getLoc(), logicType, loaded);
        Type bitsType = builder.getIntegerType(*width);
        Value bits = sim::SimLogicToBitsOp::create(
            builder, load->getLoc(), bitsType, flattened);
        Value roundTrip = sim::SimLogicFromBitsOp::create(
            builder, load->getLoc(), logicType, bits);
        Value loadKnown = sim::SimLogicCompareOp::create(
            builder, load->getLoc(), builder.getI1Type(),
            sim::CompareKind::CaseEq, flattened, roundTrip);
        knownSoFar =
            arith::AndIOp::create(builder, load->getLoc(), knownSoFar,
                                  loadKnown);
      }
      outgoingKnown[&block] = knownSoFar;
    }

    SmallVector<Operation *> branches;
    probe.walk([&](Operation *operation) {
      if (isa<cf::BranchOp, cf::CondBranchOp>(operation))
        branches.push_back(operation);
    });
    for (Operation *operation : branches) {
      Value known = outgoingKnown.lookup(operation->getBlock());
      builder.setInsertionPoint(operation);
      if (auto branch = dyn_cast<cf::BranchOp>(operation)) {
        SmallVector<Value> operands(branch.getDestOperands());
        operands.push_back(known);
        cf::BranchOp::create(builder, branch.getLoc(), branch.getDest(),
                             operands);
      } else {
        auto conditional = cast<cf::CondBranchOp>(operation);
        SmallVector<Value> trueOperands(conditional.getTrueDestOperands());
        SmallVector<Value> falseOperands(conditional.getFalseDestOperands());
        trueOperands.push_back(known);
        falseOperands.push_back(known);
        cf::CondBranchOp::create(
            builder, conditional.getLoc(), conditional.getCondition(),
            conditional.getTrueDest(), trueOperands,
            conditional.getFalseDest(),
            falseOperands);
      }
      operation->erase();
    }

    SmallVector<sim::SimReturnOp> returns;
    probe.walk([&](sim::SimReturnOp returnOp) {
      if (!checkpointBlocks.contains(returnOp->getBlock()))
        returns.push_back(returnOp);
    });
    for (sim::SimReturnOp returnOp : returns) {
      builder.setInsertionPoint(returnOp);
      Value known = outgoingKnown.lookup(returnOp->getBlock());
      Value twoState = arith::ConstantOp::create(
          builder, returnOp.getLoc(), builder.getI8Type(),
          builder.getI8IntegerAttr(1));
      Value fourState = arith::ConstantOp::create(
          builder, returnOp.getLoc(), builder.getI8Type(),
          builder.getI8IntegerAttr(0));
      Value route = arith::SelectOp::create(builder, returnOp.getLoc(), known,
                                            twoState, fourState);
      sim::SimReturnOp::create(builder, returnOp.getLoc(), route);
      returnOp.erase();
    }
    return probe;
  };

  for (sim::SimFuncOp source : sources) {
    if (!forceTwoState &&
        !variantEligibleSources.contains(source.getOperation()))
      continue;
    builder.setInsertionPointToEnd(&design.getBody().front());
    SmallString<96> base;
    (source.getSymName() + ".__obelisk_two_state").toVector(base);
    unsigned counter = 0;
    SmallString<96> name = SymbolTable::generateSymbolName<96>(
        base,
        [&](StringRef candidate) {
          return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
        },
        counter);
    uint64_t codeUnit = allocateCodeUnit();
    uint64_t sourceScope = 0;
    if (auto sourceCodeUnit =
            source->getAttrOfType<IntegerAttr>("code_unit_id")) {
      auto scope = codeUnitScopes.find(sourceCodeUnit.getUInt());
      if (scope == codeUnitScopes.end())
        return source.emitError(
                   "two-state eval source has no code-unit declaration"),
               failure();
      sourceScope = scope->second;
    }
    sim::SimCodeUnitDeclOp variantDeclaration = sim::SimCodeUnitDeclOp::create(
        builder, source.getLoc(), codeUnit, sourceScope,
        sim::EntryKind::Function, builder.getStringAttr(name),
        builder.getStringAttr("inductively two-state native eval body"),
        builder.getUnitAttr());
    Operation *cloned = source->clone();
    auto variant = cast<sim::SimFuncOp>(cloned);
    variant.setSymName(name);
    variant->setAttr("code_unit_id", builder.getI64IntegerAttr(codeUnit));
    variant->setAttr("obelisk.eval.inductive_two_state", builder.getUnitAttr());
    variant->setAttr(
        "obelisk.eval.four_state_source",
        FlatSymbolRefAttr::get(module.getContext(), source.getSymName()));
    variant.walk([&](sim::SimNBAEnqueueOp nba) {
      nba->setAttr(sim::metadata::evalCompactNBAMetadata,
                   UnitAttr::get(module.getContext()));
    });
    SymbolTable::setSymbolVisibility(variant, SymbolTable::Visibility::Private);
    SymbolTable(design).insert(cloned, design.getBody().front().end());
    source->setAttr(sim::metadata::evalTwoStateVariant,
                    FlatSymbolRefAttr::get(module.getContext(), name));
    if (source->hasAttr("obelisk.eval.inherited_two_state_checkpoint")) {
      SmallString<112> probeName;
      (source.getSymName() + ".__obelisk_path_known").toVector(probeName);
      unsigned probeCounter = 0;
      probeName = SymbolTable::generateSymbolName<112>(
          probeName,
          [&](StringRef candidate) {
            return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
          },
          probeCounter);
      uint64_t probeCodeUnit = allocateCodeUnit();
      FailureOr<sim::SimFuncOp> probe =
          materializePathKnownProbe(source, probeName, probeCodeUnit);
      if (failed(probe))
        return failure();
      if (!*probe) {
        // An empty owner-level range is sound only when a path predicate
        // guards every activation.  If that predicate needs an unsupported
        // dry-run overlay, or if it proves that no activation stays in the
        // generated closure, retain the canonical four-state route instead of
        // advertising a vacuously promotable two-state variant.  That route
        // keeps the runtime leaf inline, so the owner can no longer belong to
        // a generated eval closure; record it for the scheduler decision.
        unsupportedVariantSources.insert(source.getSymName());
        source->removeAttr(sim::metadata::evalTwoStateVariant);
        source->removeAttr("obelisk.eval.inherited_two_state_checkpoint");
        source->setAttr(sim::metadata::evalUnsupportedCheckpointOwner,
                        builder.getStringAttr(source.getSymName()));
        variant.erase();
        variantDeclaration.erase();
        continue;
      } else {
        SmallString<112> checkpointProbeName;
        (source.getSymName() + ".__obelisk_checkpoint_path")
            .toVector(checkpointProbeName);
        unsigned checkpointProbeCounter = 0;
        checkpointProbeName = SymbolTable::generateSymbolName<112>(
            checkpointProbeName,
            [&](StringRef candidate) {
              return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
            },
            checkpointProbeCounter);
        uint64_t checkpointProbeCodeUnit = allocateCodeUnit();
        FailureOr<sim::SimFuncOp> checkpointProbe =
            materializePathKnownProbe(source, checkpointProbeName,
                                      checkpointProbeCodeUnit,
                                      /*trackKnownState=*/false);
        if (failed(checkpointProbe) || !*checkpointProbe)
          return failure();
        builder.setInsertionPointToEnd(&design.getBody().front());
        sim::SimCodeUnitDeclOp::create(
            builder, source.getLoc(), probeCodeUnit, sourceScope,
            sim::EntryKind::Function, builder.getStringAttr(probeName),
            builder.getStringAttr("path-sensitive two-state entry predicate"),
            builder.getUnitAttr());
        sim::SimCodeUnitDeclOp::create(
            builder, source.getLoc(), checkpointProbeCodeUnit, sourceScope,
            sim::EntryKind::Function,
            builder.getStringAttr(checkpointProbeName),
            builder.getStringAttr("known-state checkpoint path predicate"),
            builder.getUnitAttr());
        variant->setAttr(
            "obelisk.eval.path_known_probe",
            FlatSymbolRefAttr::get(module.getContext(), probeName));
        variant->setAttr(sim::metadata::evalPathGuardedTwoState,
                         builder.getUnitAttr());
        pathProbeRoutes.push_back(builder.getDictionaryAttr(
            {builder.getNamedAttr("two_state", FlatSymbolRefAttr::get(
                                                   module.getContext(), name)),
             builder.getNamedAttr(
                 "probe",
                 FlatSymbolRefAttr::get(module.getContext(), probeName)),
             builder.getNamedAttr(
                 "checkpoint_probe",
                 FlatSymbolRefAttr::get(module.getContext(),
                                        checkpointProbeName))}));
      }
    }
    variantNames[source.getSymName()] = name.str().str();
    variants.push_back(variant);
    variantDeclarations[variant.getOperation()] =
        variantDeclaration.getOperation();
  }

  // Reject callers transitively as well.  Their cloned calls would otherwise
  // remain bound to an unsupported four-state checkpoint leaf after the
  // caller itself had entered a nominally two-state closure.
  bool removedUnsupportedCaller;
  do {
    removedUnsupportedCaller = false;
    SmallVector<sim::SimFuncOp> retained;
    for (sim::SimFuncOp variant : variants) {
      auto sourceRef = variant->getAttrOfType<FlatSymbolRefAttr>(
          "obelisk.eval.four_state_source");
      sim::SimFuncOp source =
          sourceRef ? design.lookupSymbol<sim::SimFuncOp>(sourceRef)
                    : sim::SimFuncOp{};
      bool unsupported = !source;
      if (source)
        source.walk([&](sim::SimCallOp call) {
          unsupported |= unsupportedVariantSources.contains(call.getCallee());
        });
      if (!unsupported) {
        retained.push_back(variant);
        continue;
      }
      removedUnsupportedCaller = true;
      if (source) {
        unsupportedVariantSources.insert(source.getSymName());
        source->removeAttr(sim::metadata::evalTwoStateVariant);
        variantNames.erase(source.getSymName());
      }
      if (Operation *declaration =
              variantDeclarations.lookup(variant.getOperation()))
        declaration->erase();
      variantDeclarations.erase(variant.getOperation());
      variant.erase();
    }
    variants = std::move(retained);
  } while (removedUnsupportedCaller);

  for (sim::SimFuncOp variant : variants)
    variant.walk([&](sim::SimCallOp call) {
      auto replacement = variantNames.find(call.getCallee());
      if (replacement != variantNames.end())
        call.setCalleeAttr(
            FlatSymbolRefAttr::get(module.getContext(), replacement->second));
    });
  if (!pathProbeRoutes.empty())
    module->setAttr("obelisk.eval.path_probe_routes",
                    builder.getArrayAttr(pathProbeRoutes));
  return success();
}

LogicalResult verifyGeneratedEvalCallClosures(ModuleOp module) {
  if (!module->hasAttr("obelisk.eval.generated"))
    return success();
  constexpr StringLiteral allowedCalleesAttr = "obelisk.eval.allowed_callees";
  SmallVector<LLVM::LLVMFuncOp> pending;
  llvm::SmallPtrSet<Operation *, 32> visited;
  for (LLVM::LLVMFuncOp function : module.getOps<LLVM::LLVMFuncOp>()) {
    if (!function->hasAttr(sim::metadata::evalCallClosureRoot))
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
          call.emitError("generated eval hot closure calls runtime symbol ")
              << *callee << " in " << function.getSymName();
          return WalkResult::interrupt();
        }
        targets.push_back(FlatSymbolRefAttr::get(module.getContext(), *callee));
      } else if (auto allowed =
                     call->getAttrOfType<ArrayAttr>(allowedCalleesAttr)) {
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
            module.lookupSymbol<LLVM::LLVMFuncOp>(target.getValue());
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
  };
  SmallVector<PendingEvalWrapper> pendingEvalWrappers;
  SmallVector<Attribute> checkpointRoutes;
  if (!enabled || !design)
    return result;
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
    identity.continuations.erase(
        std::unique(identity.continuations.begin(),
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
    sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (!graph)
      return coverage;
    DenseMap<Block *, uint32_t> fragmentByBlock;
    Block *anchorBlock = nullptr;
    for (Attribute attribute : graph.getNodes()) {
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
      if (!fragment || fragment.getFunction().getValue() != actor.getSymName())
        continue;
      Block *block =
          analysis::lookupComputeGraphBlock(actor, fragment.getBlock());
      if (!block)
        continue;
      uint32_t fragmentID = fragment.getId();
      fragmentByBlock.try_emplace(block, fragmentID);
      sim::ContinuationSiteAttr site;
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
    std::optional<uint32_t> actorSlot = actorSlotFor(actor);
    IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
    auto analyzed = codeUnit ? analyzedActors.find(codeUnit.getUInt())
                             : analyzedActors.end();
    if (!actorSlot || analyzed == analyzedActors.end() ||
        (!isGeneratedRegionActor(actor) && !actor->hasAttr("obelisk.eval.body")))
      continue;
    const SimulationProcessFrameAnalysis &frameAnalysis =
        *analyzed->second.analysis;
    if (auto evalBodyRef =
            actor->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body")) {
      sim::SimFuncOp evalBody =
          design.lookupSymbol<sim::SimFuncOp>(evalBodyRef.getValue());
      if (!evalBody)
        return actor.emitOpError("references a missing eval body");
      auto continuation =
          evalBody->getAttrOfType<IntegerAttr>("obelisk.eval.continuation");
      uint32_t continuationID = 0;
      ArrayRef<ProcessSuspension> suspensions =
          frameAnalysis.getSuspensions();
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
      // Keep derived MLIR metadata coherent for the two-state/checkpoint
      // variants cloned from this body later in the conversion.
      evalBody->setAttr(
          "obelisk.eval.continuation",
          IntegerAttr::get(IntegerType::get(context, 32), continuationID));
      // A direct body has no coroutine frame arguments.  Continuations with
      // live block arguments (for example `repeat (N) @(posedge clk)`) must
      // remain runtime-owned until that finite control state is exhausted.
      // Treating a cloned body as their owner drops the loop-carried value and
      // can let run_until bypass reset/stimulus continuations entirely.
      bool continuationHasArguments = false;
      actor.walk([&](Operation *operation) {
        sim::ContinuationSiteAttr site;
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
      if (auto variant = evalBody->getAttrOfType<FlatSymbolRefAttr>(
              sim::metadata::evalTwoStateVariant)) {
        twoStateBody = design.lookupSymbol<sim::SimFuncOp>(variant.getValue());
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
      sim::ContinuationSiteAttr site;
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
      if (bytecode != bytecodeContinuations.end() &&
          llvm::is_contained(bytecode->second, suspension.continuationID))
        continue;

      // A generated region kernel's entry path initializes its snapshot
      // arguments and evaluates the complete local region before reaching
      // suspend.any.  Clone that path as the direct Tier-2 activation; other
      // processes start at their ordinary resume continuation.
      Block *start = generatedRegionBody ? &actor.getBody().front()
                                         : suspension.continuation;
      auto isTerminalWait = [&](Block *block) {
        Operation *terminator = block->getTerminator();
        sim::ContinuationSiteAttr site;
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
          [&](StringRef name) {
            return SymbolTable::lookupSymbolIn(design, name) != nullptr;
          },
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
      body->setAttr("obelisk.eval.borrowed_captures", UnitAttr::get(context));
      body->setAttr("obelisk.eval.raw_captures", UnitAttr::get(context));
      if (Attribute owners = actor->getAttr("obelisk.eval.source_owners"))
        body->setAttr("obelisk.eval.source_owners", owners);
      if (Attribute group = actor->getAttr("obelisk.eval.fusion_group"))
        body->setAttr("obelisk.eval.fusion_group", group);
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
    if (failed(makeDirectFragmentWrapper(
            module, pending.body, pending.actor, pending.wrapper,
            pending.actorSlot, pending.continuation, *pending.analysis)))
      return failure();
    if (pending.twoStateBody && failed(makeDirectFragmentWrapper(
                                    module, pending.twoStateBody, pending.actor,
                                    pending.twoStateWrapper, pending.actorSlot,
                                    pending.continuation, *pending.analysis)))
      return failure();
    if (pending.body->hasAttr("obelisk.eval.inherited_two_state_checkpoint")) {
      Builder builder(context);
      checkpointRoutes.push_back(builder.getDictionaryAttr(
          {builder.getNamedAttr(
               "four_state",
               FlatSymbolRefAttr::get(context, pending.body.getSymName())),
           builder.getNamedAttr("actor",
                                builder.getI32IntegerAttr(pending.actorSlot)),
           builder.getNamedAttr(
               "continuation",
               builder.getI32IntegerAttr(pending.continuation))}));
    }
    bool initialActivation = pending.initialActivation.value_or(false);
    SmallVector<NativePromotionRange> localPromotionRanges;
    if (pending.twoStateBody) {
      auto encoded = pending.twoStateBody->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.eval.local_promotion_ranges");
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
      if (pending.twoStateBody->hasAttr(
              sim::metadata::evalPathGuardedTwoState) &&
          !pending.twoStateBody->hasAttr(
              sim::metadata::evalPathGuardedKnownPreserving))
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
                design.lookupSymbol<sim::SimFuncOp>(call.getCallee()))
          ownerClosure.push_back(callee);
      });
    }
    auto collectSourceOwners = [&](sim::SimFuncOp function) -> LogicalResult {
      ArrayAttr owners = function->getAttrOfType<ArrayAttr>(
          "obelisk.eval.source_owners");
      if (!owners)
        return success();
      for (Attribute attribute : owners) {
        auto owner = dyn_cast<DictionaryAttr>(attribute);
        auto codeUnit =
            owner ? owner.getAs<IntegerAttr>("code_unit") : IntegerAttr{};
        auto continuation =
            owner ? owner.getAs<IntegerAttr>("continuation") : IntegerAttr{};
        if (!codeUnit || codeUnit.getInt() < 0 || !continuation ||
            continuation.getInt() <= 0 ||
            static_cast<uint64_t>(continuation.getInt()) > UINT32_MAX)
          return function.emitOpError("has malformed stable eval source owner");
        uint32_t continuationID =
            static_cast<uint32_t>(continuation.getInt());
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
      auto group = function->getAttrOfType<IntegerAttr>(
          "obelisk.eval.fusion_group");
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
        {pending.actorSlot, pending.continuation, std::move(pending.wrapper),
         std::move(pending.twoStateWrapper),
         pending.twoStateBody ? pending.twoStateBody.getSymName().str()
                              : std::string{},
         std::move(sourceOwners), std::move(sourceCodeUnits),
         std::move(pending.fragmentIDs),
         std::move(localPromotionRanges), fusionGroup, initialActivation});
  }
  if (!checkpointRoutes.empty())
    module->setAttr(sim::metadata::evalCheckpointRoutes,
                    ArrayAttr::get(context, checkpointRoutes));
  return result;
}

LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  if (failed(prepareManagedLowering(module, dataLayout)))
    return failure();
  FailureOr<NativeStateLayout> stateLayout = buildNativeStateLayout(module);
  if (failed(stateLayout))
    return failure();
  sim::StaticSpecializationAttr staticSpecialization;
  sim::StaticSuperstepAttr staticSuperstep;
  SmallVector<sim::ComputeNBACommitAttr> staticNBACommits;
  sim::SimDesignOp metadataDesign;
  module.walk([&](sim::SimDesignOp design) {
    metadataDesign = design;
    staticSuperstep = design->getAttrOfType<sim::StaticSuperstepAttr>(
        sim::metadata::staticSuperstep);
  });
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(metadataDesign);
  // Resolved nets and driver contributions occupy the same canonical native
  // planes as storage.  With no external writer their fixed handles are
  // always safe to address directly; publication and resolution still flow
  // through the ordinary scheduler boundaries.
  bool hasLanguageOverride = false;
  module.walk([&](Operation *operation) {
    hasLanguageOverride |=
        isa<sim::SimOverrideOp, sim::SimReleaseOverrideOp>(operation);
  });
  if (vpi.getMode() == sim::ComputeVPIMode::Off && !hasLanguageOverride) {
    auto authorizeFixedHandles = [&](const auto &descriptors) {
      for (const auto &[descriptor, handle] : descriptors) {
        (void)descriptor;
        obelisk_rt_stable_handle_v1 decoded{};
        if (obelisk_rt_stable_handle_decode(handle, &decoded) &&
            decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
            decoded.offset == 0)
          stateLayout->directHandles.insert(decoded.id);
      }
    };
    authorizeFixedHandles(stateLayout->nets);
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
  sim::NativeSchedulerMode nativeScheduler = sim::NativeSchedulerMode::Auto;
  if (auto mode = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
          "obelisk.native_scheduler"))
    nativeScheduler = mode.getValue();
  analysis::NativeAOTAnalysis aotEligibility;
  bool useAOT = false;
  bool evalScheduler = nativeScheduler == sim::NativeSchedulerMode::Eval;
  DenseMap<Operation *, SmallVector<uint32_t>> aotBytecodeContinuations;
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
    if (!suspendable && !process)
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
  // Fixed root-spawn captures are useful independently of scheduler
  // selection: replacing a proven-unique storage capture with its context
  // lookup exposes a constant stable handle to direct-state lowering.  The
  // AOT analysis also records dynamic/duplicate actors, so the same proof is
  // safe for the generic scheduler.
  aotEligibility = analysis::NativeAOTAnalysis::compute(module);
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
  if (nativeScheduler != sim::NativeSchedulerMode::Generic) {
    bool forcedAOT =
        nativeScheduler == sim::NativeSchedulerMode::AOT || evalScheduler;
    useAOT = aotEligibility.isEligible() &&
             (forcedAOT || aotEligibility.isAOTCostEffective());
    if (forcedAOT && !aotEligibility.isFullyEligible()) {
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
  if (staticSuperstep && useAOT && aotEligibility.isFullyEligible()) {
    ArrayAttr actors = staticSuperstep.getActors();
    if (actors.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "native lowering rejected stale static-superstep actor inventory");
    for (auto [slot, attribute] : llvm::enumerate(actors)) {
      auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
      sim::SimFuncOp function =
          actor ? metadataDesign.lookupSymbol<sim::SimFuncOp>(actor.getValue())
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
    cleanSuperstep = true;
  }
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
  // Read-only VPI still publishes the runtime-owned planes to plugins. Until
  // those planes are the generated code's canonical storage, keep reads and
  // writes on the coherent helper path in that mode. Full VPI uses guarded
  // specialization, while language force/release likewise requires helpers.
  directStaticState = staticSpecialization && vpi.hasComputeGraph() &&
                      vpi.getMode() != sim::ComputeVPIMode::Read &&
                      !hasLanguageOverride &&
                      (!stateLayout->directHandles.empty() ||
                       !stateLayout->guardedHandles.empty());
  if (useAOT && aotEligibility.isFullyEligible()) {
    staticControl = vpi.hasComputeGraph();
    staticFanoutMetadata = vpi.hasComputeGraph();
    // Read-only VPI observes the same canonical planes but cannot mutate
    // roots or invalidate the closed-world waiter inventory. It therefore
    // uses the fully static fanout schedule just like VPI-off.
    staticFanout = vpi.preservesStaticDependencies();
    staticNBA = staticSpecialization && !stateLayout->nbaHandles.empty();
  }
  if (staticControl) {
    module.walk([&](Operation *operation) {
      if (llvm::any_of(operation->getOperandTypes(),
                       [](Type type) { return isa<FloatType>(type); }) ||
          llvm::any_of(operation->getResultTypes(),
                       [](Type type) { return isa<FloatType>(type); })) {
        staticControl = false;
        staticFanout = false;
        staticFanoutMetadata = false;
      }
    });
  }
  // State, NBA, and fanout are independent capabilities. Direct access is
  // selected per operation by resolveDirectStaticStateRange; a wide or
  // otherwise generic root does not prevent an independent narrow root from
  // using generated planes.
  if (staticNBA) {
    FailureOr<NativeStaticNBAPlan> plan =
        buildNativeStaticNBAPlan(module, *stateLayout, staticNBACommits, true);
    if (failed(plan))
      return failure();
    staticNBAPlan = std::move(*plan);
    staticNBA = !staticNBAPlan.roots.empty();
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
  if (staticFanoutMetadata) {
    FailureOr<NativeStaticFanoutPlan> fanout = buildNativeStaticFanoutPlan(
        module, *stateLayout, aotEligibility.getActorSlots(), true);
    if (failed(fanout))
      return failure();
    staticFanoutPlan = std::move(*fanout);
    staticFanoutMetadata &= staticFanoutPlan.exact;
    staticFanout &= staticFanoutPlan.exact;
    if (staticFanoutPlan.exact) {
      stateLayout->transitionHandlesExact = true;
      for (const obelisk_rt_static_fanout_entry &entry :
           staticFanoutPlan.entries)
        stateLayout->transitionHandles.insert(entry.static_state);
    }
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
  // Auto selects the generated eval form only after the physical periodic
  // clocks and the closed-world slot proof exist.  Explicit Eval remains a
  // hard request; Auto can still fall back to the legacy AOT coordinator.
  if (nativeScheduler == sim::NativeSchedulerMode::Auto)
    evalScheduler =
        !periodicClocks.empty() && cleanSuperstep && staticFanoutPlan.exact;
  if (staticSpecialization && useAOT) {
    FailureOr<SmallVector<obelisk_rt_static_actor_root>> dependencies =
        buildNativeStaticActorRootPlan(module, *stateLayout,
                                       aotEligibility.getActorSlots());
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
  FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
      analysis::SimulationScheduleAnalysis::compute(module);
  if (failed(scheduleRanks))
    return failure();
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
        if (activationRequiresBytecode(suspension.continuation))
          continuations.push_back(suspension.continuationID);
      llvm::sort(continuations);
      continuations.erase(
          std::unique(continuations.begin(), continuations.end()),
          continuations.end());
    }
  }
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

  if (failed(materializeEvalTwoStateVariants(module, metadataDesign,
                                             *stateLayout, evalScheduler)))
    return failure();

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
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t>
      preLowerFusionOwners;
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
    if (actor->hasAttr(sim::metadata::nativeRegionBody) &&
        actor->hasAttr(sim::metadata::evalReconstructsContinuationArgs)) {
      IntegerAttr codeUnit = actor.getCodeUnitIdAttr();
      if (!codeUnit)
        return actor.emitOpError(
                   "generated native region has no stable code-unit ID"),
               failure();
      preLowerGeneratedRegionCodeUnits.insert(codeUnit.getUInt());
    }
    auto group = actor->getAttrOfType<IntegerAttr>(
        "obelisk.eval.fusion_group");
    if (!group)
      continue;
    if (group.getInt() < 0 ||
        static_cast<uint64_t>(group.getInt()) > UINT32_MAX)
      return actor.emitOpError("has an invalid eval fusion group"), failure();
    uint32_t groupID = static_cast<uint32_t>(group.getUInt());
    if (ArrayAttr owners = actor->getAttrOfType<ArrayAttr>(
            "obelisk.eval.source_owners"))
      for (Attribute attribute : owners) {
        auto owner = dyn_cast<DictionaryAttr>(attribute);
        auto codeUnit =
            owner ? owner.getAs<IntegerAttr>("code_unit") : IntegerAttr{};
        if (!codeUnit || codeUnit.getInt() < 0)
          return actor.emitOpError("has a malformed eval source owner"),
                 failure();
        auto [entry, inserted] =
            preLowerFusionSourceCodeUnits.try_emplace(codeUnit.getUInt(),
                                                      groupID);
        if (!inserted && entry->second != groupID) {
          actor.emitError("source code unit appears in multiple fusion groups");
          invalidPreLowerFusion = true;
        }
      }
    actor.walk([&](Operation *operation) {
      sim::ContinuationSiteAttr site;
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

  FailureOr<SmallVector<NativeDirectFragment>> directFragments =
      materializeDirectFragments(module, metadataDesign,
                                 aotActorSlotsByCodeUnit,
                                 analyses, aotBytecodeContinuations,
                                 preLowerGeneratedRegionCodeUnits,
                                 useAOT && cleanSuperstep && staticFanout &&
                                     !guardedAOTSpecialization);
  if (failed(directFragments))
    return failure();

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
    ArrayAttr fusions = metadataDesign->getAttrOfType<ArrayAttr>(
        sim::metadata::staticFusion);
    sim::ComputeGraphAttr graph = metadataDesign.getComputeGraphAttr();
    if (fusions && graph) {
      for (Attribute fusionAttribute : fusions) {
        auto fusion = dyn_cast<sim::ComputeFusionAttr>(fusionAttribute);
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
    if (direct.fusionGroup != UINT32_MAX)
      for (const auto &[sourceOwner, group] : aotFusionGroups)
        if (group == direct.fusionGroup)
          direct.sourceOwners.push_back(sourceOwner);
    llvm::sort(direct.sourceOwners);
    direct.sourceOwners.erase(
        std::unique(direct.sourceOwners.begin(), direct.sourceOwners.end()),
        direct.sourceOwners.end());
    for (auto sourceOwner : direct.sourceOwners)
      if (auto fragments = staticFanoutPlan.fragments.find(sourceOwner);
          fragments != staticFanoutPlan.fragments.end())
        llvm::append_range(direct.fragmentIDs, fragments->second);
    llvm::sort(direct.fragmentIDs);
    direct.fragmentIDs.erase(
        std::unique(direct.fragmentIDs.begin(), direct.fragmentIDs.end()),
        direct.fragmentIDs.end());
  }

  NativeEvalOwnershipPlan evalOwnership;
  if (evalScheduler) {
    FailureOr<NativeEvalOwnershipPlan> ownership = buildNativeEvalOwnershipPlan(
        module, *stateLayout, staticFanoutPlan, *directFragments,
        periodicAliases);
    if (failed(ownership))
      return failure();
    evalOwnership = std::move(*ownership);
    // Exact unfused actor/continuation ownership is resolved against the
    // current static-fanout plan. Transfer that plan's current-generation
    // fragment coverage to the direct body so downstream closure/SCC
    // analysis sees the same physical nodes. This replaces the old unsafe
    // FragmentABI ordinal fallback.
    for (auto [entryIndex, entry] :
         llvm::enumerate(staticFanoutPlan.entries)) {
      if (entryIndex >= evalOwnership.fanoutOwners.size())
        return module.emitError("eval ownership plan is incomplete"),
               failure();
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
      auto wrapper = module.lookupSymbol<LLVM::LLVMFuncOp>(direct.wrapper);
      if (!wrapper)
        continue;
      auto owner = wrapper->getAttrOfType<StringAttr>(
          sim::metadata::evalUnsupportedCheckpointOwner);
      if (!owner)
        continue;
      unsupportedCheckpointOwner =
          "an eval owner keeps an unguarded runtime leaf in " +
          owner.getValue().str();
      break;
    }
    if (!unsupportedCheckpointOwner.empty()) {
      if (nativeScheduler != sim::NativeSchedulerMode::Auto)
        return module.emitError(unsupportedCheckpointOwner), failure();
      module.emitRemark("generated eval disabled: ")
          << unsupportedCheckpointOwner;
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
    for (const NativeThreeTierKernelPlan &kernel : threeTierPlan.kernels) {
      if (kernel.tier != sim::SchedulerTierKind::Tier2 ||
          kernel.schedule != sim::ComputeScheduleKind::Convergence)
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
        invalidConvergenceOwnership =
            "a Tier-2 SCC has no direct eval owner";
        break;
      }
    }
    if (!invalidConvergenceOwnership.empty()) {
      if (nativeScheduler != sim::NativeSchedulerMode::Auto)
        return module.emitError(invalidConvergenceOwnership), failure();
      module.emitRemark("generated eval disabled: ")
          << invalidConvergenceOwnership;
      evalScheduler = false;
      for (NativeDirectFragment &direct : *directFragments)
        direct.tier2Convergence = false;
    }
  }

  if (evalScheduler && !periodicClocks.empty()) {
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
        sim::ContinuationSiteAttr site;
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
      if (isFiniteInitialBootstrap(entry.actor_slot, entry.continuation))
        continue;
      if (entryIndex >= evalOwnership.fanoutOwners.size())
        return module.emitError("eval ownership plan is incomplete"), failure();
      const NativeEvalFanoutOwner &owner =
          evalOwnership.fanoutOwners[entryIndex];
      if (owner.kind == NativeEvalFanoutOwnerKind::PeriodicAlias)
        continue;
      const NativeDirectFragment *direct =
          owner.kind == NativeEvalFanoutOwnerKind::Direct &&
                  owner.directFragment < directFragments->size()
              ? &(*directFragments)[owner.directFragment]
              : nullptr;
      if (!direct || direct->wrapper.empty()) {
        auto diagnostic = nativeScheduler == sim::NativeSchedulerMode::Auto
                              ? module.emitRemark(
                                    "auto eval exact owner miss: actor=")
                              : module.emitError(
                                    "eval exact owner miss: actor=");
        diagnostic << entry.actor_slot << " continuation="
                   << entry.continuation;
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
            diagnostic << "[" << candidate.continuation << " group="
                       << candidate.fusionGroup << " wrapper="
                       << candidate.wrapper << ":";
            for (uint32_t fragment : candidate.fragmentIDs)
              diagnostic << fragment << ",";
            diagnostic << " owners=";
            for (auto [actor, continuation] : candidate.sourceOwners)
              diagnostic << actor << "/" << continuation << ",";
            diagnostic << "]";
          }
        if (nativeScheduler != sim::NativeSchedulerMode::Auto)
          return failure();
        evalScheduler = false;
        break;
      }
      executors.insert(direct->wrapper);
    }
    // The first serial coordinator represents ingress in one machine word.
    // Wider generated ready sets remain a future ABI-compatible extension.
    if (executors.empty() || executors.size() > 64)
      evalScheduler = false;
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
      sim::SimFuncOp body =
          metadataDesign.lookupSymbol<sim::SimFuncOp>(direct.twoStateBody);
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
          sim::SimFuncOp callee =
              metadataDesign.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          if (callee && callee->hasAttr("obelisk.eval.inductive_two_state"))
            pending.push_back(callee);
        });
      }
      for (sim::SimFuncOp selected : closure)
        selected->setAttr("obelisk.eval.selected_two_state",
                          UnitAttr::get(context));
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
    else if (physicalGroup != UINT32_MAX &&
             direct.fusionGroup != physicalGroup)
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
      rankedAOTNodes.emplace_back(
          scheduleRanks->getBlockRank(&function.getBody().front()).value_or(0),
          *schedule.actorSlot, 0, UINT32_MAX);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        rankedAOTNodes.emplace_back(
            scheduleRanks->getBlockRank(suspension.continuation).value_or(0),
            *schedule.actorSlot, suspension.continuationID,
            fusionGroupFor(*schedule.actorSlot, suspension.continuationID));
    }
    if (failed(makeProcessActivationHelper(module, function, *entry.second)))
      return failure();
    if (failed(
            makeProcessSpawnHelper(module, function, *entry.second, schedule)))
      return failure();
  }
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
      if (failed(makeNativeEvalPlan(
              module, aotEligibility.getActorSlots().size(), executableNodes,
              *stateLayout, staticNBAPlan, staticFanoutPlan, staticActorRoots,
              *directFragments, evalOwnership, threeTierPlan.sourceGraph,
              periodicClocks, periodicAliases, directStaticState, staticNBA,
              staticControl, staticFanout,
              cleanSuperstep, aotEligibility.isFullyEligible(), rootSlotZero,
              vpi)))
        return failure();
    } else if (failed(makeNativeAOTPlanLegacy(
                   module, aotEligibility.getActorSlots().size(),
                   executableNodes, *stateLayout, staticNBAPlan,
                   staticFanoutPlan, staticActorRoots, directStaticState,
                   staticNBA, staticControl, staticFanout, cleanSuperstep,
                   aotEligibility.isFullyEligible(), rootSlotZero, vpi))) {
      return failure();
    }
  }
  if (failed(makeSchedulerMain(module, *stateLayout, useAOT, evalScheduler)))
    return failure();

  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      ordinary.push_back(function);
  });
  for (sim::SimFuncOp function : ordinary)
    if (failed(lowerOrdinaryFunction(function)))
      return failure();
  // Runtime status checks can make an ordinary direct body return i32 while
  // its wrapper was formed against the pre-runtime void signature. Reconcile
  // the explicitly tagged private call after every ordinary signature is
  // final, and propagate the cold-path status through the wrapper.
  SmallVector<func::CallOp> directCalls;
  module.walk([&](func::CallOp call) {
    if (call->hasAttr("obelisk.eval.direct_call"))
      directCalls.push_back(call);
  });
  for (func::CallOp call : directCalls) {
    auto callee = module.lookupSymbol<func::FuncOp>(call.getCallee());
    if (!callee && metadataDesign)
      callee = metadataDesign.lookupSymbol<func::FuncOp>(call.getCallee());
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

  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    LogicalResult lowered =
        entry.second->getSuspensions().empty()
            ? lowerPlainNativeProcess(function, *entry.second)
            : lowerSuspendableProcess(function, *entry.second);
    if (failed(lowered))
      return failure();
  }

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  for (sim::SimDesignOp design : designs) {
    SmallVector<Operation *> nested;
    for (Operation &operation : design.getBody().front())
      nested.push_back(&operation);
    for (Operation *operation : nested) {
      if (isa<sim::SimScopeDeclOp, sim::SimCodeUnitDeclOp,
              sim::SimStorageDeclOp, sim::SimNetDeclOp, sim::SimDriverDeclOp,
              sim::SimNetConnectDeclOp, sim::SimClassDeclOp,
              sim::SimCovergroupDeclOp, sim::SimClassFieldDeclOp,
              sim::SimClassMethodDeclOp>(operation)) {
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
  llvm::StringMap<std::string> pathKnownProbes;
  llvm::StringMap<std::string> checkpointPathProbes;
  llvm::StringMap<std::pair<uint32_t, uint32_t>> checkpointOwners;
  if (ArrayAttr mappings =
          module->getAttrOfType<ArrayAttr>("obelisk.eval.path_probe_routes")) {
    for (Attribute mappingAttr : mappings) {
      auto mapping = dyn_cast<DictionaryAttr>(mappingAttr);
      auto twoState = mapping ? mapping.getAs<FlatSymbolRefAttr>("two_state")
                              : FlatSymbolRefAttr{};
      auto probe = mapping ? mapping.getAs<FlatSymbolRefAttr>("probe")
                           : FlatSymbolRefAttr{};
      auto checkpointProbe =
          mapping ? mapping.getAs<FlatSymbolRefAttr>("checkpoint_probe")
                  : FlatSymbolRefAttr{};
      if (!twoState || !probe || !checkpointProbe)
        return module.emitError("has malformed eval path-probe route"),
               failure();
      pathKnownProbes[twoState.getValue()] = probe.getValue().str();
      checkpointPathProbes[twoState.getValue()] =
          checkpointProbe.getValue().str();
    }
    module->removeAttr("obelisk.eval.path_probe_routes");
  }
  if (ArrayAttr mappings =
          module->getAttrOfType<ArrayAttr>(sim::metadata::evalCheckpointRoutes)) {
    for (Attribute mappingAttr : mappings) {
      auto mapping = dyn_cast<DictionaryAttr>(mappingAttr);
      auto fourState =
          mapping ? mapping.getAs<FlatSymbolRefAttr>("four_state")
                  : FlatSymbolRefAttr{};
      auto actor = mapping ? mapping.getAs<IntegerAttr>("actor")
                           : IntegerAttr{};
      auto continuation = mapping
                              ? mapping.getAs<IntegerAttr>("continuation")
                              : IntegerAttr{};
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
    module->removeAttr(sim::metadata::evalCheckpointRoutes);
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
    auto source = function->getAttrOfType<FlatSymbolRefAttr>(
        "obelisk.eval.four_state_source");
    auto ranges = function->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk.eval.local_promotion_ranges");
    if (!source || !ranges)
      return;
    LLVM::LLVMFuncOp fourState =
        module.lookupSymbol<LLVM::LLVMFuncOp>(source.getValue());
    if (!fourState || fourState.getFunctionType() != function.getFunctionType())
      return;
    LLVM::LLVMFuncOp pathKnownProbe;
    LLVM::LLVMFuncOp checkpointPathProbe;
    auto probeName = pathKnownProbes.find(function.getSymName());
    if (probeName != pathKnownProbes.end()) {
      pathKnownProbe = module.lookupSymbol<LLVM::LLVMFuncOp>(probeName->second);
      if (!pathKnownProbe) {
        function.emitError("has no lowered path-known probe ")
            << probeName->second;
        routeError = true;
        return;
      }
      auto checkpointProbeName =
          checkpointPathProbes.find(function.getSymName());
      if (checkpointProbeName == checkpointPathProbes.end() ||
          !(checkpointPathProbe = module.lookupSymbol<LLVM::LLVMFuncOp>(
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
         function->hasAttr("obelisk.eval.conditionally_two_state"),
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
      module.lookupSymbol<LLVM::LLVMFuncOp>("__obelisk_aot_schedule_run_v1");
  if (!run || run.empty())
    return module.emitError("eval function routes have no periodic run loop");
  LLVM::CallOp prepare;
  run.walk([&](LLVM::CallOp call) {
    if (call.getCallee() &&
        *call.getCallee() == "obelisk_rt_v1_scheduler_prepare_periodic_aot")
      prepare = call;
  });
  if (!prepare) {
    auto scheduler = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
        "obelisk.native_scheduler");
    if (scheduler && scheduler.getValue() == sim::NativeSchedulerMode::Auto)
      return success();
    return run.emitError("eval function routes have no Tier-2 handoff");
  }

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  constexpr StringLiteral routePromotionDirtyName =
      "__obelisk_eval_route_promotion_dirty_v1";
  constexpr StringLiteral routePromotionScanName =
      "__obelisk_eval_route_promotion_scan_v1";
  constexpr StringLiteral routePromotionBoundaryName =
      "__obelisk_eval_route_promotion_boundary_v1";
  builder.setInsertionPointToStart(module.getBody());
  LLVM::GlobalOp::create(builder, module.getLoc(), i8, false,
                         LLVM::Linkage::Internal, routePromotionDirtyName,
                         builder.getI8IntegerAttr(1), 1);
  for (Route &route : routes) {
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
      route.checkpointBody = cast<LLVM::LLVMFuncOp>(
          route.fourState->clone());
      route.checkpointBody.setSymName(route.checkpointBodyName);
      route.checkpointBody.setPrivate();
      module.getBody()->push_back(route.checkpointBody);

      auto fractureCheckpoints = [&](LLVM::LLVMFuncOp function) {
        llvm::MapVector<Block *, Operation *> checkpointBlocks;
        function.walk([&](LLVM::CallOp call) {
          if (std::optional<StringRef> callee = call.getCallee();
              callee && callee->starts_with("obelisk_rt_"))
            checkpointBlocks.try_emplace(call->getBlock(),
                                         call.getOperation());
        });
        if (checkpointBlocks.empty())
          return function.emitError(
                     "path-sensitive route has no checkpoint leaf"),
                 failure();
        for (auto [block, firstCheckpoint] : checkpointBlocks) {
        // Keep unsupported calls out of the generated closure. The route
        // probe must intercept this path before entering either body; this
        // return is a defensive failure path, not the runtime continuation.
        builder.setInsertionPoint(firstCheckpoint);
        for (Operation *operation = firstCheckpoint; operation;
             operation = operation->getNextNode())
          for (Value result : operation->getResults())
            if (!result.use_empty())
              result.replaceAllUsesWith(
                  LLVM::PoisonOp::create(builder, operation->getLoc(),
                                         result.getType()));
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
        return success();
      };
      if (failed(fractureCheckpoints(route.twoState)) ||
          failed(fractureCheckpoints(route.fourState)))
        return failure();

      LLVM::LLVMFuncOp resumeCoordinator =
          module.lookupSymbol<LLVM::LLVMFuncOp>(
              detail::evalHybridCoordinatorName);
      LLVM::GlobalOp mutableState = module.lookupSymbol<LLVM::GlobalOp>(
          detail::evalCheckpointMutableStateName);
      if (!resumeCoordinator || !mutableState)
        return route.fourState.emitError(
            "checkpoint route has no same-slot generated resume");

      // The runtime invokes this cold thunk outside the generated hot call
      // graph. Execute the original activation once, then resume the same
      // generated slot coordinator so staged NBAs and downstream ready bits
      // reach the shared barrier before the next periodic edge.
      builder.setInsertionPointToEnd(module.getBody());
      route.checkpointFallback = LLVM::LLVMFuncOp::create(
          builder, route.fourState.getLoc(), route.fourStateFallbackName,
          bodyType);
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
      if (auto fallback = module.lookupSymbol<LLVM::GlobalOp>(
              "__obelisk_eval_step_four_state_fallback_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fallback.getSymName()),
            1);
      if (auto latched = module.lookupSymbol<LLVM::GlobalOp>(
              "__obelisk_eval_fast_nba_latched_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 0),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, latched.getSymName()),
            1);
      if (auto fastRoots = module.lookupSymbol<LLVM::GlobalOp>(
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
          LLVM::AddressOfOp::create(
              builder, route.fourState.getLoc(), pointer,
              detail::evalCheckpointContinuationName),
          4);
      Value nextCallback = LLVM::LoadOp::create(
          builder, route.fourState.getLoc(), pointer,
          LLVM::AddressOfOp::create(builder, route.fourState.getLoc(), pointer,
                                    detail::evalCheckpointCallbackName),
          8);
      Value queueStatus =
          LLVM::CallOp::create(
              builder, route.fourState.getLoc(), TypeRange{builder.getI32Type()},
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
          builder, route.fourState.getLoc(), queued, resumeStatus,
          queueStatus);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             publishedStatus);
      builder.setInsertionPointToStart(returnResumeStatus);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             returnResumeStatus->getArgument(0));
      builder.setInsertionPointToStart(returnBodyStatus);
      LLVM::ReturnOp::create(builder, route.fourState.getLoc(),
                             returnBodyStatus->getArgument(0));

      builder.setInsertionPointToEnd(module.getBody());
      route.dispatcher = LLVM::LLVMFuncOp::create(
          builder, route.twoState.getLoc(), route.dispatcherName, bodyType);
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
      route.dispatcher.getBody().push_back(twoState);
      route.dispatcher.getBody().push_back(fourState);
      route.dispatcher.getBody().push_back(fullProbe);
      route.dispatcher.getBody().push_back(knownStateProbe);
      route.dispatcher.getBody().push_back(probeJoin);
      route.dispatcher.getBody().push_back(classifyNative);
      route.dispatcher.getBody().push_back(checkpoint);
      builder.setInsertionPointToStart(entry);
      SmallVector<Value> arguments(entry->getArguments());
      Value promoted = LLVM::ICmpOp::create(
          builder, route.twoState.getLoc(), LLVM::ICmpPredicate::ne,
          LLVM::LoadOp::create(
              builder, route.twoState.getLoc(), i8,
              LLVM::AddressOfOp::create(
                  builder, route.twoState.getLoc(), pointer,
                  "__obelisk_eval_periodic_promotion_latched_v1"),
              1),
          detail::llvmConstant(builder, route.twoState.getLoc(), i8, 0));
      LLVM::CondBrOp::create(builder, route.twoState.getLoc(), promoted,
                             knownStateProbe, fullProbe);
      builder.setInsertionPointToStart(fullProbe);
      Value fullPath = LLVM::CallOp::create(builder, route.twoState.getLoc(),
                                            route.pathKnownProbe, arguments)
                           .getResult();
      LLVM::BrOp::create(builder, route.twoState.getLoc(),
                         ValueRange{fullPath}, probeJoin);
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
          LLVM::AddressOfOp::create(
              builder, route.twoState.getLoc(), pointer,
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
          if (auto fallback = module.lookupSymbol<LLVM::GlobalOp>(
                  "__obelisk_eval_step_four_state_fallback_v1"))
            LLVM::StoreOp::create(
                builder, route.fourState.getLoc(),
                detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
                LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                          pointer, fallback.getSymName()),
                1);
          if (auto latched = module.lookupSymbol<LLVM::GlobalOp>(
                  "__obelisk_eval_fast_nba_latched_v1"))
            LLVM::StoreOp::create(
                builder, route.fourState.getLoc(),
                detail::llvmConstant(builder, route.fourState.getLoc(), i8, 0),
                LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                          pointer, latched.getSymName()),
                1);
          if (auto fastRoots = module.lookupSymbol<LLVM::GlobalOp>(
                  "__obelisk_eval_fast_nba_roots_v1"))
            LLVM::StoreOp::create(
                builder, route.fourState.getLoc(),
                LLVM::ZeroOp::create(builder, route.fourState.getLoc(),
                                     fastRoots.getGlobalType()),
                LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                          pointer, fastRoots.getSymName()),
                8);
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
      route.fourStateFallback.setPrivate();
      Block *entry = route.fourStateFallback.addEntryBlock(builder);
      builder.setInsertionPointToStart(entry);
      if (auto fallback = module.lookupSymbol<LLVM::GlobalOp>(
              "__obelisk_eval_step_four_state_fallback_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 1),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fallback.getSymName()),
            1);
      if (auto latched = module.lookupSymbol<LLVM::GlobalOp>(
              "__obelisk_eval_fast_nba_latched_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            detail::llvmConstant(builder, route.fourState.getLoc(), i8, 0),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, latched.getSymName()),
            1);
      if (auto fastRoots = module.lookupSymbol<LLVM::GlobalOp>(
              "__obelisk_eval_fast_nba_roots_v1"))
        LLVM::StoreOp::create(
            builder, route.fourState.getLoc(),
            LLVM::ZeroOp::create(builder, route.fourState.getLoc(),
                                 fastRoots.getGlobalType()),
            LLVM::AddressOfOp::create(builder, route.fourState.getLoc(),
                                      pointer, fastRoots.getSymName()),
            8);
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

    // prepare_periodic_aot drains the transient Tier-2 startup prefix and may
    // invoke the generated coordinator.  Seed every route with its four-state
    // body before that call; replace it with the proven boundary selection
    // only after the drain reaches quiescence.
    builder.setInsertionPoint(prepare);
    StringRef fallback = route.dispatcher ? route.dispatcher.getSymName()
                         : route.fourStateFallback
                             ? route.fourStateFallback.getSymName()
                             : route.fourState.getSymName();
    LLVM::StoreOp::create(
        builder, route.twoState.getLoc(),
        LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                  fallback),
        LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                  route.globalName),
        8);
    builder.setInsertionPointAfter(prepare);
    Location location = route.twoState.getLoc();
    Value unknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_unknown");
    Value anyUnknown = detail::llvmConstant(builder, location, i8, 0);
    ArrayRef<int64_t> encoded = route.ranges.asArrayRef();
    if ((encoded.size() & 1) != 0)
      return route.twoState.emitError("malformed local promotion ranges");
    for (size_t index = 0; index != encoded.size(); index += 2) {
      if (encoded[index] < 0 || encoded[index + 1] <= 0)
        return route.twoState.emitError("invalid local promotion range");
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
                                   route.independentEntry)
            : LLVM::ICmpOp::create(
                  builder, location, LLVM::ICmpPredicate::eq, anyUnknown,
                  detail::llvmConstant(builder, location, i8, 0));
    Value selected =
        route.pathKnownProbe
            ? LLVM::AddressOfOp::create(builder, location, pointer, fallback)
                  .getResult()
            : LLVM::SelectOp::create(
                  builder, location, known,
                  LLVM::AddressOfOp::create(builder, location, pointer,
                                            route.twoState.getSymName()),
                  LLVM::AddressOfOp::create(builder, location, pointer,
                                            fallback))
                  .getResult();
    LLVM::StoreOp::create(
        builder, location, selected,
        LLVM::AddressOfOp::create(builder, location, pointer, route.globalName),
        8);
  }

  // Canonical and four-state NBA barriers are the generated synchronous
  // boundaries that can establish a new known invariant. Mark their route
  // closure dirty once; the successful coordinator return below performs the
  // scan after the combined NBA barrier and post-NBA fanout have quiesced.
  SmallVector<LLVM::CallOp> transitionNBACalls;
  module.walk([&](LLVM::CallOp call) {
    if (call->hasAttr("obelisk.eval.use_canonical_two_state_nba") ||
        call->hasAttr("obelisk.eval.keep_four_state_nba"))
      transitionNBACalls.push_back(call);
  });
  for (LLVM::CallOp call : transitionNBACalls) {
    builder.setInsertionPointAfter(call);
    LLVM::StoreOp::create(builder, call.getLoc(),
                          detail::llvmConstant(builder, call.getLoc(), i8, 1),
                          LLVM::AddressOfOp::create(builder, call.getLoc(),
                                                    pointer,
                                                    routePromotionDirtyName),
                          1);
  }

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
  LLVM::StoreOp::create(builder, module.getLoc(),
                        detail::llvmConstant(builder, module.getLoc(), i8, 0),
                        LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                  pointer,
                                                  routePromotionDirtyName),
                        1);
  for (Route &route : routes) {
    if (route.pathKnownProbe ||
        (route.ranges.empty() && !route.independentEntry))
      continue;
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
            ? detail::llvmConstant(builder, location, builder.getI1Type(), true)
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
    LLVM::StoreOp::create(
        builder, location, selected,
        LLVM::AddressOfOp::create(builder, location, pointer, route.globalName),
        8);
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
  Value dirty = LLVM::LoadOp::create(
      builder, module.getLoc(), i8,
      LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                routePromotionDirtyName),
      1);
  Value isDirty = LLVM::ICmpOp::create(
      builder, module.getLoc(), LLVM::ICmpPredicate::ne, dirty,
      detail::llvmConstant(builder, module.getLoc(), i8, 0));
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

  llvm::SmallDenseSet<StringRef, 8> coordinatorNames{
      "__obelisk_eval_fast_coordinator_v1",
      "__obelisk_eval_fast_coordinator_two_state_v1",
      "__obelisk_eval_steady_two_state_coordinator_v1",
      "__obelisk_eval_periodic_two_state_coordinator_v1",
      "__obelisk_eval_fast_coordinator_hybrid_v1"};
  SmallVector<LLVM::CallOp> coordinatorCalls;
  run.walk([&](LLVM::CallOp call) {
    if (call.getNumResults() == 1 && call.getCallee() &&
        coordinatorNames.contains(*call.getCallee()))
      coordinatorCalls.push_back(call);
  });
  for (LLVM::CallOp call : coordinatorCalls) {
    builder.setInsertionPointAfter(call);
    auto boundaryCall =
        LLVM::CallOp::create(builder, call.getLoc(), routePromotionBoundary,
                             ValueRange{call.getResult()});
    for (OpOperand &use :
         llvm::make_early_inc_range(call.getResult().getUses()))
      if (use.getOwner() != boundaryCall.getOperation())
        use.set(boundaryCall.getResult());
  }

  llvm::StringMap<Route *> routesByFunction;
  for (Route &route : routes) {
    routesByFunction[route.fourState.getSymName()] = &route;
    routesByFunction[route.twoState.getSymName()] = &route;
  }

  // An asynchronous X/Z handoff clears the model-wide promotion latch. Reset
  // every non-vacuous local route in the same generated invalidator so the
  // four-state coordinator cannot retain a stale two-state leaf selection.
  if (LLVM::LLVMFuncOp invalidate = module.lookupSymbol<LLVM::LLVMFuncOp>(
          "__obelisk_eval_promotion_invalidate_v1")) {
    SmallVector<LLVM::ReturnOp> returns;
    invalidate.walk(
        [&](LLVM::ReturnOp returnOp) { returns.push_back(returnOp); });
    for (LLVM::ReturnOp returnOp : returns) {
      builder.setInsertionPoint(returnOp);
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

  // The first quiescent check runs before synchronous reset has necessarily
  // established the owner's known-state invariant.  When the generated
  // periodic prefix later latches, recheck each route's own closure before
  // selecting its two-state body.  The model-wide latch is only a scan trigger:
  // it is not evidence that every independently routed instance is known.
  // Empty helper contracts continue to inherit their caller's four-state ABI
  // rather than being promoted without an entry precondition.
  if (LLVM::LLVMFuncOp promotion = module.lookupSymbol<LLVM::LLVMFuncOp>(
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
      Value ready = returnOp.getOperand(0);
      for (Route &route : routes) {
        if (route.pathKnownProbe)
          continue;
        if (route.ranges.size() == 0 && !route.independentEntry)
          continue;
        Location location = returnOp.getLoc();
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
            anyUnknown =
                LLVM::OrOp::create(builder, location, anyUnknown, bits);
          }
        }
        Value locallyKnown =
            encoded.empty()
                ? detail::llvmConstant(builder, location, builder.getI1Type(),
                                       route.independentEntry)
                : LLVM::ICmpOp::create(
                      builder, location, LLVM::ICmpPredicate::eq, anyUnknown,
                      detail::llvmConstant(builder, location, i8, 0));
        Value promote =
            LLVM::AndOp::create(builder, location, ready, locallyKnown);
        Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  route.globalName);
        Value existing =
            LLVM::LoadOp::create(builder, location, pointer, address, 8);
        Value selected = LLVM::SelectOp::create(
            builder, location, promote,
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      route.twoState.getSymName()),
            existing);
        LLVM::StoreOp::create(builder, location, selected, address, 8);
      }
    }
  }

  // Specialize only the wrappers reached from a trusted, post-promotion
  // coordinator. Transient/hybrid execution must retain mutable route edges,
  // but sharing those wrappers with the steady coordinator prevents LLVM from
  // seeing the direct per-instance call graph and blocks normal inlining.
  // Cloning the small wrapper boundary keeps the body itself shared while
  // making the quiescent certificate explicit in the IR.
  SmallVector<LLVM::CallOp> trustedWrapperCalls;
  module.walk([&](LLVM::LLVMFuncOp function) {
    if (!function->hasAttr(sim::metadata::evalTrustedTwoStateCoordinator))
      return;
    function.walk([&](LLVM::CallOp call) {
      if (!call.getCallee())
        return;
      LLVM::LLVMFuncOp callee =
          module.lookupSymbol<LLVM::LLVMFuncOp>(*call.getCallee());
      if (callee &&
          callee->getAttrOfType<UnitAttr>(sim::metadata::evalTwoStateVariant))
        trustedWrapperCalls.push_back(call);
    });
  });
  llvm::DenseMap<Operation *, LLVM::LLVMFuncOp> trustedWrapperClones;
  for (LLVM::CallOp call : trustedWrapperCalls) {
    LLVM::LLVMFuncOp wrapper =
        module.lookupSymbol<LLVM::LLVMFuncOp>(*call.getCallee());
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
      while (module.lookupSymbol<LLVM::LLVMFuncOp>(name)) {
        name = base;
        (Twine("_") + Twine(++suffix)).toVector(name);
      }
      Operation *detached = wrapper->clone();
      clone = cast<LLVM::LLVMFuncOp>(detached);
      clone.setSymName(name);
      clone->setAttr("obelisk.eval.trusted_two_state_closure",
                     builder.getUnitAttr());
      SymbolTable(module).insert(detached);
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
        caller && (caller->hasAttr("obelisk.eval.four_state_source") ||
                   caller->hasAttr("obelisk.eval.trusted_two_state_closure"));
    if (selectedTwoStateClosure && !route.dispatcher) {
      // The outer coordinator has already established the quiescent
      // owner boundary. Preserve a direct edge so LLVM can inline across
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
    replacement->setAttr(
        "obelisk.eval.allowed_callees", builder.getArrayAttr([&] {
          SmallVector<Attribute> allowed{
              FlatSymbolRefAttr::get(context, route.twoState.getSymName())};
          if (!route.pathKnownProbe)
            allowed.push_back(FlatSymbolRefAttr::get(
                context, route.fourState.getSymName()));
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
    bool twoState = function->hasAttr("obelisk.eval.four_state_source") ||
                    function->hasAttr(sim::metadata::evalTwoStateVariant) ||
                    function->hasAttr("obelisk.eval.selected_two_state");
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
        load && load->hasAttr("obelisk.eval.two_state_zero_unknown"))
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
  // Specialization is carried entirely by call-site intent.  Validate and
  // consume the phase-local markers so a renamed or newly outlined
  // coordinator cannot silently retain the wrong NBA implementation.
  WalkResult rewrite = module.walk([&](LLVM::CallOp call) {
    bool useFast = call->hasAttr("obelisk.eval.use_fast_two_state_nba");
    bool useCanonical =
        call->hasAttr("obelisk.eval.use_canonical_two_state_nba");
    bool keepFourState = call->hasAttr("obelisk.eval.keep_four_state_nba");
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
      call.setCallee(fastTwoStateName);
      call->removeAttr("obelisk.eval.use_fast_two_state_nba");
      return WalkResult::advance();
    }
    if (useCanonical) {
      if (!expectedSource && !expectedCanonical && !expectedFast)
        return call.emitError("canonical two-state NBA intent is attached to "
                              "an unrelated callee"),
               WalkResult::interrupt();
      call.setCallee(twoStateName);
      call->removeAttr("obelisk.eval.use_canonical_two_state_nba");
      return WalkResult::advance();
    }
    if (!expectedSource)
      return call.emitError("four-state NBA intent is attached to an "
                            "unrelated callee"),
             WalkResult::interrupt();
    call->removeAttr("obelisk.eval.keep_four_state_nba");
    return WalkResult::advance();
  });
  if (rewrite.wasInterrupted())
    return failure();
  source.walk([](LLVM::LoadOp load) {
    load->removeAttr("obelisk.eval.two_state_zero_unknown");
  });
  return success();
}

class ConvertObeliskSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertObeliskSimProcessesToLLVMCoroutinesPassBase<
          ConvertObeliskSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
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
    if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64) {
      module.emitError("coroutine lowering currently requires a 64-bit "
                       "little-endian target");
      return signalPassFailure();
    }
    if (failed(validateProcessABI(module, *parsed)))
      return signalPassFailure();
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();
    if (failed(materializeEmbeddedSimulationDesign(module)))
      return signalPassFailure();

    if (failed(prepareSimulationProcessesToLLVMCoroutines(module, *parsed)))
      return signalPassFailure();

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
    RewritePatternSet patterns(&getContext());
    populateSimulationCoroutineToLLVMPatterns(converter, patterns);
    if (failed(verify(module)))
      return signalPassFailure();
    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp>();
    target.markUnknownOpDynamicallyLegal(
        [](Operation *operation) { return isa<LLVM::LLVMFuncOp>(operation); });
    if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
    }
    if (failed(materializeEvalFunctionRoutes(module))) {
      signalPassFailure();
      return;
    }
    if (failed(materializeEvalTwoStateNBACommit(module))) {
      signalPassFailure();
      return;
    }
    if (failed(materializeNativeObserverThunks(module))) {
      signalPassFailure();
      return;
    }
    if (failed(verifyGeneratedEvalCallClosures(module))) {
      signalPassFailure();
      return;
    }
    if (failed(verify(module)))
      signalPassFailure();
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
  populateContextRuntimeToLLVMConversionPattern(patterns, converter);
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateMathToLLVMConversionPatterns(converter, patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns);
}

} // namespace obelisk
