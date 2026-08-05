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
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

#include <algorithm>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

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
using detail::materializeNativeThreeTierPlan;
using detail::materializeNativeSchedulerGlobals;
using detail::NativeDirectFragment;
using detail::NativePeriodicClock;
using detail::NativePeriodicAlias;
using detail::NativeSchedulePlan;
using detail::NativePromotionRange;
using detail::NativeStateLayout;
using detail::NativeStaticFanoutPlan;
using detail::NativeStaticNBAPlan;
using detail::NativeThreeTierPlan;
using detail::NativeThreeTierKernelPlan;
using detail::populateContextRuntimeToLLVMConversionPattern;
using detail::prepareManagedLowering;
using detail::specializeNativeAOTCaptures;
using detail::threadProcessStateThroughCFG;
using detail::validateProcessABI;

LogicalResult materializeEvalTwoStateVariants(
    ModuleOp module, sim::SimDesignOp design,
    const detail::NativeStateLayout &stateLayout, bool enabled,
    SmallVectorImpl<detail::NativePromotionRange> &promotionRanges) {
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
    sim::SimFuncOp target = design.lookupSymbol<sim::SimFuncOp>(body.getValue());
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
    auto selectRange = [&](Value handle,
                           const analysis::DescriptorProvenanceMap &provenance,
                           llvm::SmallDenseSet<PhysicalRange, 8> &localRanges)
        -> bool {
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
        if (isa<sim::SimFileOpenMCDOp, sim::SimFileOpenOp,
                sim::SimFileCloseOp,
                sim::SimFileFlushOp, sim::SimFileGetcOp,
                sim::SimFileUngetcOp, sim::SimFileGetlineOp,
                sim::SimFileReadPackedOp, sim::SimFileEofOp,
              sim::SimFileSeekOp, sim::SimFileTellOp,
              sim::SimFileRewindOp>(operation)) {
          preserving = false;
          runtimeFree = false;
          checkpointSafe = false;
          return;
        }
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
          preserving &=
              knownStateDomains->isTwoStateWithInductiveRoots(
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
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            store.getValue()) &&
                        selectRange(store.getReference(), provenance,
                                    localRanges);
          return;
        }
        if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          auto found = provenance.find(nba.getDestination());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                              *found->second.descriptor))
            (void)selectRange(nba.getDestination(), provenance,
                              inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            nba.getValue()) &&
                        selectRange(nba.getDestination(), provenance,
                                    localRanges);
          return;
        }
        if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
          auto found = provenance.find(drive.getDriver());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                              *found->second.descriptor))
            (void)selectRange(drive.getDriver(), provenance,
                              inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            drive.getValue()) &&
                        selectRange(drive.getDriver(), provenance,
                                    localRanges);
          return;
        }
        if (auto drive = dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
          auto found = provenance.find(drive.getDriver());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                              *found->second.descriptor))
            (void)selectRange(drive.getDriver(), provenance,
                              inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            drive.getValue()) &&
                        selectRange(drive.getDriver(), provenance,
                                    localRanges);
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
      SmallVector<PhysicalRange> orderedInductiveRanges(
          inductiveRanges.begin(), inductiveRanges.end());
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
        closureRuntimeFree &= locallyRuntimeFree.lookup(function.getOperation());
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
          if (callee != source &&
              callee->hasAttr("obelisk.eval.raw_captures"))
            return;
          closure.push_back(callee);
        });
      }
      for (BlockArgument argument : source.getBody().front().getArguments())
        if (detail::containsLogic(argument.getType()))
          closureKnownPreserving &=
              domains->isTwoState(argument);
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
      if (!closureRuntimeFree)
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
          if (callee->hasAttr(
                  "obelisk.eval.inherited_two_state_checkpoint"))
            return;
          for (PhysicalRange range :
               routeClosureRanges[callee.getOperation()])
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
        if (!operation->hasAttr(
                "obelisk.eval.inherited_two_state_checkpoint"))
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
    for (auto [offset, width] : selectedRanges)
      promotionRanges.push_back({offset, width});
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
  auto allocateCodeUnit = [&] {
    while (usedCodeUnits.contains(nextCodeUnit))
      ++nextCodeUnit;
    usedCodeUnits.insert(nextCodeUnit);
    return nextCodeUnit++;
  };

  for (sim::SimFuncOp source : sources) {
    if (!forceTwoState &&
        !variantEligibleSources.contains(source.getOperation()))
      continue;
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
    sim::SimCodeUnitDeclOp::create(
        builder, source.getLoc(), codeUnit, sourceScope,
        sim::EntryKind::Function, builder.getStringAttr(name),
        builder.getStringAttr("inductively two-state native eval body"),
        builder.getUnitAttr());
    Operation *cloned = builder.clone(*source.getOperation());
    auto variant = cast<sim::SimFuncOp>(cloned);
    variant.setSymName(name);
    variant->setAttr("code_unit_id", builder.getI64IntegerAttr(codeUnit));
    variant->setAttr("obelisk.eval.inductive_two_state", builder.getUnitAttr());
    variant->setAttr("obelisk.eval.four_state_source",
                     FlatSymbolRefAttr::get(module.getContext(),
                                            source.getSymName()));
    SymbolTable::setSymbolVisibility(variant,
                                     SymbolTable::Visibility::Private);
    source->setAttr("obelisk.eval.two_state_variant",
                    FlatSymbolRefAttr::get(module.getContext(), name));
    variantNames[source.getSymName()] = name.str().str();
    variants.push_back(variant);
  }
  for (sim::SimFuncOp variant : variants)
    variant.walk([&](sim::SimCallOp call) {
      auto replacement = variantNames.find(call.getCallee());
      if (replacement != variantNames.end())
        call.setCalleeAttr(FlatSymbolRefAttr::get(module.getContext(),
                                                  replacement->second));
    });
  return success();
}

LogicalResult verifyGeneratedEvalCallClosures(ModuleOp module) {
  if (!module->hasAttr("obelisk.eval.generated"))
    return success();
  constexpr StringLiteral allowedCalleesAttr =
      "obelisk.eval.allowed_callees";
  SmallVector<LLVM::LLVMFuncOp> pending;
  llvm::SmallPtrSet<Operation *, 32> visited;
  for (LLVM::LLVMFuncOp function : module.getOps<LLVM::LLVMFuncOp>()) {
    StringRef name = function.getSymName();
    bool generatedHot =
        name.starts_with("__obelisk_eval_fast_coordinator") ||
        name.starts_with("__obelisk_eval_periodic_two_state_coordinator") ||
        name.starts_with("__obelisk_tier1_eval_") ||
        name.starts_with("__obelisk_tier2_converge_");
    if (!generatedHot)
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
      } else if (auto allowed = call->getAttrOfType<ArrayAttr>(
                     allowedCalleesAttr)) {
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
        // The three-tier planning ABI deliberately models fragment execution
        // as a callback.  It is not installed in the production periodic hot
        // coordinator; every emitted production route below carries an exact
        // allowed-callee set and is checked transitively.
        if (!function.getSymName().starts_with("__obelisk_tier")) {
          call.emitError(
              "generated eval indirect call has no closed target set in ")
              << function.getSymName();
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      }
      for (FlatSymbolRefAttr target : targets) {
        if (target.getValue().starts_with("obelisk_rt_")) {
          call.emitError("generated eval indirect route targets runtime "
                         "symbol ")
              << target.getValue();
          return WalkResult::interrupt();
        }
        if (LLVM::LLVMFuncOp callee =
                module.lookupSymbol<LLVM::LLVMFuncOp>(target.getValue())) {
          // Checkpoint-capable owners stay on the coordinator path and retain
          // its status/termination edge.  They are not part of the closed
          // direct Tier-1/Tier-2 call graph proved by this verifier.
          if (!callee->hasAttr("obelisk.eval.may_terminate"))
            pending.push_back(callee);
        }
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
    const analysis::NativeAOTAnalysis &eligibility,
    const llvm::MapVector<
        Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>> &analyses,
    const DenseMap<Operation *, SmallVector<uint32_t>> &bytecodeContinuations,
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
  if (!enabled || !design)
    return result;
  MLIRContext *context = module.getContext();
  auto fragmentIDsFor = [&](sim::SimFuncOp actor, uint32_t continuation) {
    SmallVector<uint32_t> ids;
    sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (!graph)
      return ids;
    for (auto [index, attribute] : llvm::enumerate(graph.getNodes())) {
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
      if (!fragment || fragment.getFunction().getValue() != actor.getSymName())
        continue;
      Block *block = analysis::lookupComputeGraphBlock(actor, fragment.getBlock());
      if (!block)
        continue;
      sim::ContinuationSiteAttr site;
      if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(block->getTerminator()))
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
      if (site && site.getId() == continuation)
        ids.push_back(static_cast<uint32_t>(index));
    }
    // FragmentABI is only a fallback when the body has no representation in
    // the current graph.  Never combine it with current graph ordinals: body
    // fusion rebuilds the graph, so equal integers from the two generations do
    // not identify the same physical fragment.
    if (ids.empty())
      if (sim::FragmentABIAttr abi = actor.getFragmentAbiAttr())
        for (int64_t fragment : abi.getFragments().asArrayRef())
          if (fragment >= 0 && static_cast<uint64_t>(fragment) <= UINT32_MAX)
            ids.push_back(static_cast<uint32_t>(fragment));
    llvm::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
  };
  for (const auto &entry : analyses) {
    auto actor = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    auto actorSlot = eligibility.getActorSlots().find(entry.first);
    if (!actor || actorSlot == eligibility.getActorSlots().end() ||
        (!actor->hasAttr(sim::metadata::nativeRegionBody) &&
         !actor->hasAttr("obelisk.eval.body")))
      continue;
    if (auto evalBodyRef =
            actor->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body")) {
      sim::SimFuncOp evalBody =
          design.lookupSymbol<sim::SimFuncOp>(evalBodyRef.getValue());
      if (!evalBody)
        return actor.emitOpError("references a missing eval body");
      auto continuation = evalBody->getAttrOfType<IntegerAttr>(
          "obelisk.eval.continuation");
      uint32_t continuationID = 0;
      if (continuation && continuation.getInt() > 0 &&
          static_cast<uint64_t>(continuation.getInt()) <= UINT32_MAX) {
        continuationID = static_cast<uint32_t>(continuation.getInt());
      } else {
        // Older or externally supplied eval bodies may not yet carry the
        // stable site attribute.  Their owning process analysis already
        // records continuation IDs independently of transformed operations;
        // a unique activation is therefore an unambiguous, pointer-free
        // recovery path.
        for (const ProcessSuspension &suspension :
             entry.second->getSuspensions()) {
          if (continuationID != 0) {
            continuationID = 0;
            break;
          }
          continuationID = suspension.continuationID;
        }
        if (continuationID == 0)
          continue;
        evalBody->setAttr("obelisk.eval.continuation",
                          IntegerAttr::get(IntegerType::get(context, 32),
                                           continuationID));
      }
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
      if (continuationHasArguments &&
          actor.getEntryKind() == sim::EntryKind::Initial)
        continue;
      SmallString<96> wrapperName;
      (Twine("__obelisk_direct_fragment_") + Twine(actorSlot->second) + "_" +
       Twine(continuationID))
          .toVector(wrapperName);
      std::string wrapper = (Twine(wrapperName) + ".__obelisk_execute").str();
      sim::SimFuncOp twoStateBody;
      std::string twoStateWrapper;
      if (auto variant = evalBody->getAttrOfType<FlatSymbolRefAttr>(
              "obelisk.eval.two_state_variant")) {
        twoStateBody = design.lookupSymbol<sim::SimFuncOp>(variant.getValue());
        if (!twoStateBody)
          return evalBody.emitOpError("references a missing two-state body");
        twoStateWrapper = wrapper + ".two_state";
      }
      pendingEvalWrappers.push_back(
          {evalBody, twoStateBody, actor, std::move(wrapper),
           std::move(twoStateWrapper), actorSlot->second,
           continuationID, entry.second.get(),
           /*initialActivation=*/false,
           fragmentIDsFor(actor, continuationID)});
      continue;
    }
    struct CurrentDirectWait {
      Operation *operation;
      Block *continuation;
      uint32_t continuationID;
    };
    bool generatedRegionBody =
        actor->hasAttr(sim::metadata::nativeRegionBody);
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
      auto bytecode = bytecodeContinuations.find(entry.first);
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
        else if (auto suspend =
                     dyn_cast<sim::SimSuspendObserveOp>(terminator))
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
      // The experimental eval scheduler deliberately clones the complete
      // acyclic activation CFG.  Its purpose is to measure the ceiling of a
      // Verilator-shaped whole-cycle evaluator, so imposing the production
      // code-size profitability cap here would leave the main clocked body in
      // the fine actor dispatcher and invalidate the experiment.
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
      (Twine("__obelisk_direct_fragment_") + Twine(actorSlot->second) + "_" +
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
      pendingEvalWrappers.push_back(
          {body, {}, actor, std::move(wrapper), {}, actorSlot->second,
           suspension.continuationID, entry.second.get(),
           generatedRegionBody,
           fragmentIDsFor(actor, suspension.continuationID)});
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
    if (pending.twoStateBody &&
        failed(makeDirectFragmentWrapper(
            module, pending.twoStateBody, pending.actor,
            pending.twoStateWrapper, pending.actorSlot, pending.continuation,
            *pending.analysis)))
      return failure();
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
    }
    SmallVector<std::pair<uint32_t, uint32_t>> sourceOwners;
    if (auto owners = pending.actor->getAttrOfType<ArrayAttr>(
            "obelisk.eval.source_owners")) {
      for (Attribute attribute : owners) {
        auto owner = dyn_cast<DictionaryAttr>(attribute);
        auto codeUnit = owner ? owner.getAs<IntegerAttr>("code_unit")
                              : IntegerAttr{};
        auto continuation =
            owner ? owner.getAs<IntegerAttr>("continuation") : IntegerAttr{};
        if (!codeUnit || codeUnit.getInt() < 0 || !continuation ||
            continuation.getInt() <= 0 ||
            static_cast<uint64_t>(continuation.getInt()) > UINT32_MAX)
          return pending.actor.emitOpError(
              "has malformed stable eval source owner");
        auto slot = eligibility.getActorSlots().end();
        bool ambiguous = false;
        for (auto candidate = eligibility.getActorSlots().begin();
             candidate != eligibility.getActorSlots().end(); ++candidate) {
          auto function = dyn_cast_if_present<sim::SimFuncOp>(candidate->first);
          IntegerAttr candidateCodeUnit =
              function ? function.getCodeUnitIdAttr() : IntegerAttr{};
          if (!candidateCodeUnit ||
              candidateCodeUnit.getInt() != codeUnit.getInt())
            continue;
          if (slot != eligibility.getActorSlots().end()) {
            ambiguous = true;
            break;
          }
          slot = candidate;
        }
        if (ambiguous)
          return pending.actor.emitOpError(
              "has ambiguous stable eval source code unit");
        if (slot == eligibility.getActorSlots().end())
          continue;
        sourceOwners.emplace_back(
            slot->second, static_cast<uint32_t>(continuation.getInt()));
      }
      llvm::sort(sourceOwners);
      sourceOwners.erase(std::unique(sourceOwners.begin(), sourceOwners.end()),
                         sourceOwners.end());
    }
    result.push_back({pending.actorSlot, pending.continuation,
                      std::move(pending.wrapper),
                      std::move(pending.twoStateWrapper),
                      pending.twoStateBody
                          ? pending.twoStateBody.getSymName().str()
                          : std::string{},
                      std::move(sourceOwners),
                      std::move(pending.fragmentIDs),
                      {}, std::move(localPromotionRanges), UINT32_MAX,
                      initialActivation});
  }
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
                 false,
                 *stateLayout);
  makeStatePlane(module, "__obelisk_state_unknown",
                 stateBytes + stateGuardBytes, true,
                 *stateLayout);
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
    evalScheduler = !periodicClocks.empty() && cleanSuperstep &&
                    staticFanoutPlan.exact;
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
    if (failed(materializeNativeThreeTierPlan(module, threeTierPlan)))
      return failure();
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

  SmallVector<detail::NativePromotionRange> evalPromotionRanges;
  if (failed(materializeEvalTwoStateVariants(
          module, metadataDesign, *stateLayout, evalScheduler,
          evalPromotionRanges)))
    return failure();

  bool enableDirectStaticState = directStaticState;
  if (failed(lowerPackedSimulationOperations(
          module, dataLayout, *stateLayout, enableDirectStaticState,
          staticNBA ? &staticNBAPlan : nullptr, vpi.allowsWrite(),
          /*experimentalTwoState=*/false)))
    return failure();

  FailureOr<SmallVector<NativeDirectFragment>> directFragments =
      materializeDirectFragments(
          module, metadataDesign, aotEligibility, analyses,
          aotBytecodeContinuations,
          useAOT && cleanSuperstep && staticFanout &&
              !guardedAOTSpecialization);
  if (failed(directFragments))
    return failure();

  if (nativeScheduler == sim::NativeSchedulerMode::Auto && evalScheduler) {
    SmallVector<sim::SimFuncOp> actorsBySlot(
        aotEligibility.getActorSlots().size());
    for (auto actor : aotEligibility.getActorSlots())
      if (actor.second < actorsBySlot.size())
        actorsBySlot[actor.second] =
            dyn_cast_if_present<sim::SimFuncOp>(actor.first);
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
        else if (auto suspend =
                     dyn_cast<sim::SimSuspendObserveOp>(operation))
          site = suspend.getSiteAttr();
        if (site && site.getId() == continuation &&
            operation->getNumSuccessors() == 1)
          loopCarriedContinuation |=
              operation->getSuccessor(0)->getNumArguments() != 0;
      });
      return loopCarriedContinuation;
    };
    llvm::SmallDenseSet<StringRef, 16> executors;
    for (const obelisk_rt_static_fanout_entry &entry :
         staticFanoutPlan.entries) {
      // A finite initial loop is deliberately runtime-owned while it carries
      // repeat/control state. Periodic handoff drains this bootstrap prefix
      // and checks that no such subscription remains live before entering the
      // generated loop, so it is not a member of the steady-state owner set.
      if (isFiniteInitialBootstrap(entry.actor_slot, entry.continuation))
        continue;
      bool forwardingAlias = llvm::any_of(
          periodicAliases, [&](const NativePeriodicAlias &alias) {
            if (alias.sourceStaticState != entry.static_state ||
                alias.forwardingActorSlot != entry.actor_slot ||
                alias.forwardingContinuation != entry.continuation)
              return false;
            auto bound = llvm::find_if(
                stateLayout->bounds, [&](const auto &candidate) {
                  return candidate.handleID == entry.static_state;
                });
            if (bound == stateLayout->bounds.end() ||
                alias.sourceBitOffset < bound->offset)
              return false;
            uint64_t localBit = alias.sourceBitOffset - bound->offset;
            return entry.low_bit <= localBit &&
                   localBit - entry.low_bit < entry.bit_width;
          });
      if (forwardingAlias)
        continue;
      auto direct = directFragments->end();
      direct = llvm::find_if(*directFragments, [&](const auto &candidate) {
        return candidate.actorSlot == entry.actor_slot &&
               candidate.continuation == entry.continuation;
      });
      if (direct == directFragments->end()) {
        auto fragments = staticFanoutPlan.fragments.find(
            {entry.actor_slot, entry.continuation});
        if (fragments != staticFanoutPlan.fragments.end() &&
            !fragments->second.empty()) {
          auto best = directFragments->end();
          bool bestActorMatch = false;
          size_t bestSize = SIZE_MAX;
          bool ambiguous = false;
          for (auto candidate = directFragments->begin();
               candidate != directFragments->end(); ++candidate) {
            // Fusion may transfer several source actors into one generated
            // owner. Physical fragment containment is stable across that
            // transfer; the original actor/continuation pair is not.
            if (!llvm::all_of(fragments->second, [&](uint32_t fragment) {
                  return llvm::is_contained(candidate->fragmentIDs, fragment);
                }))
              continue;
            bool actorMatch = candidate->actorSlot == entry.actor_slot;
            if (best == directFragments->end() ||
                actorMatch > bestActorMatch ||
                (actorMatch == bestActorMatch &&
                 candidate->fragmentIDs.size() < bestSize)) {
              best = candidate;
              bestActorMatch = actorMatch;
              bestSize = candidate->fragmentIDs.size();
              ambiguous = false;
            } else if (actorMatch == bestActorMatch &&
                       candidate->fragmentIDs.size() == bestSize)
              ambiguous = true;
          }
          direct = ambiguous ? directFragments->end() : best;
        }
      }
      if (direct == directFragments->end() || direct->wrapper.empty()) {
        auto diagnostic = module.emitRemark("auto eval exact owner miss: actor=");
        diagnostic << entry.actor_slot << " continuation="
                   << entry.continuation << " planned=";
        if (auto planned = staticFanoutPlan.fragments.find(
                {entry.actor_slot, entry.continuation});
            planned != staticFanoutPlan.fragments.end())
          for (uint32_t fragment : planned->second)
            diagnostic << fragment << ",";
        diagnostic << " candidates=";
        for (const auto &candidate : *directFragments)
          if (candidate.actorSlot == entry.actor_slot) {
            diagnostic << "[" << candidate.continuation << ":";
            for (uint32_t fragment : candidate.fragmentIDs)
              diagnostic << fragment << ",";
            diagnostic << "]";
          }
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
          sim::SimFuncOp callee = metadataDesign.lookupSymbol<sim::SimFuncOp>(
              call.getCallee());
          if (callee &&
              callee->hasAttr("obelisk.eval.inductive_two_state"))
            pending.push_back(callee);
        });
      }
      for (sim::SimFuncOp selected : closure)
        selected->setAttr("obelisk.eval.selected_two_state",
                          UnitAttr::get(context));
    }
    llvm::sort(evalPromotionRanges, [](const auto &lhs, const auto &rhs) {
      return std::tie(lhs.bitOffset, lhs.bitWidth) <
             std::tie(rhs.bitOffset, rhs.bitWidth);
    });
    evalPromotionRanges.erase(
        std::unique(evalPromotionRanges.begin(), evalPromotionRanges.end(),
                    [](const auto &lhs, const auto &rhs) {
                      return lhs.bitOffset == rhs.bitOffset &&
                             lhs.bitWidth == rhs.bitWidth;
                    }),
        evalPromotionRanges.end());
  }
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> aotFusionGroups;
  DenseMap<uint32_t, uint32_t> fragmentFusionGroups;
  if (useAOT) {
    sim::SimDesignOp design;
    module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
    ArrayAttr fusions =
        design ? design->getAttrOfType<ArrayAttr>(sim::metadata::staticFusion)
               : ArrayAttr{};
    sim::ComputeGraphAttr graph =
        design ? design.getComputeGraphAttr() : nullptr;
    if (fusions && graph) {
      for (Attribute fusionAttribute : fusions) {
        auto fusion = dyn_cast<sim::ComputeFusionAttr>(fusionAttribute);
        if (!fusion)
          return design.emitOpError("has malformed static fusion metadata"),
                 failure();
        for (int64_t fragmentIndex : fusion.getFragments().asArrayRef()) {
          if (fragmentIndex < 0 ||
              static_cast<uint64_t>(fragmentIndex) >= graph.getNodes().size())
            return design.emitOpError(
                       "static fusion references an invalid compute fragment"),
                   failure();
          fragmentFusionGroups[static_cast<uint32_t>(fragmentIndex)] =
              fusion.getId();
          auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
              graph.getNodes()[static_cast<size_t>(fragmentIndex)]);
          sim::SimFuncOp function = fragment
                                        ? design.lookupSymbol<sim::SimFuncOp>(
                                              fragment.getFunction().getValue())
                                        : nullptr;
          Block *block = function ? analysis::lookupComputeGraphBlock(
                                        function, fragment.getBlock())
                                  : nullptr;
          auto actor =
              function
                  ? aotEligibility.getActorSlots().find(function.getOperation())
                  : aotEligibility.getActorSlots().end();
          sim::ContinuationSiteAttr site;
          if (block) {
            Operation *terminator = block->getTerminator();
            if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
              site = suspend.getSiteAttr();
            else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
              site = suspend.getSiteAttr();
          }
          if (!fragment || !block || !site)
            return design.emitOpError(
                       "static fusion references a stale AOT continuation"),
                   failure();
          // Fusion metadata describes graph-level opportunities and is built
          // before native AOT actor eligibility is known. Hybrid lowering must
          // retain valid bytecode-only fragments without treating them as
          // stale metadata.
          if (actor == aotEligibility.getActorSlots().end())
            continue;
          auto [entry, inserted] = aotFusionGroups.try_emplace(
              std::pair{actor->second, site.getId()}, fusion.getId());
          if (!inserted && entry->second != fusion.getId())
            return design.emitOpError(
                       "AOT continuation appears in multiple fusion groups"),
                   failure();
        }
      }
    }
  }
  auto fusionGroupFor = [&](uint32_t slot, uint32_t continuation) {
    auto found = aotFusionGroups.find({slot, continuation});
    return found == aotFusionGroups.end() ? UINT32_MAX : found->second;
  };
  for (NativeDirectFragment &direct : *directFragments) {
    direct.fusionGroup =
        fusionGroupFor(direct.actorSlot, direct.continuation);
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
    direct.tier2Convergence = llvm::any_of(
        threeTierPlan.kernels, [&](const NativeThreeTierKernelPlan &kernel) {
          return kernel.tier == sim::SchedulerTierKind::Tier2 &&
                 kernel.schedule == sim::ComputeScheduleKind::Convergence &&
                 llvm::any_of(direct.fragmentIDs, [&](uint32_t fragment) {
                   return llvm::is_contained(kernel.memberIDs, fragment);
                 });
        });
  }

  SmallVector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>
      rankedAOTNodes;
  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    NativeSchedulePlan schedule;
    schedule.initialRank = scheduleRanks->getEntryRank(entry.first).value_or(0);
    if (useAOT) {
      auto slot = aotEligibility.getActorSlots().find(entry.first);
      if (slot != aotEligibility.getActorSlots().end())
        schedule.actorSlot = slot->second;
    }
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
              *directFragments, threeTierPlan.sourceGraph,
              periodicClocks, periodicAliases,
              evalPromotionRanges, directStaticState, staticNBA, staticControl,
              staticFanout,
              cleanSuperstep, true,
              aotEligibility.isFullyEligible(), rootSlotZero, vpi)))
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
      return call.emitError() << "direct eval body is missing: "
                              << call.getCallee(),
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
    auto replacement = func::CallOp::create(
        callBuilder, call.getLoc(), call.getCalleeAttr(), results,
        call.getOperands());
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
  struct Route {
    LLVM::LLVMFuncOp fourState;
    LLVM::LLVMFuncOp twoState;
    DenseI64ArrayAttr ranges;
    bool independentEntry = false;
    std::string globalName;
  };
  SmallVector<Route> routes;
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
    routes.push_back(
        {fourState, function, ranges,
         function->hasAttr("obelisk.eval.conditionally_two_state"),
         (Twine("__obelisk_eval_function_route_v1_") +
          Twine(routes.size()))
             .str()});
  });
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
  if (!prepare)
    return run.emitError("eval function routes have no Tier-2 handoff");

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  for (Route &route : routes) {
    builder.setInsertionPointToStart(module.getBody());
    auto global = LLVM::GlobalOp::create(
        builder, route.twoState.getLoc(), pointer, false,
        LLVM::Linkage::Internal, route.globalName, Attribute{}, 8);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(builder, route.twoState.getLoc(),
                           LLVM::ZeroOp::create(builder,
                                                route.twoState.getLoc(),
                                                pointer));

    // prepare_periodic_aot drains the transient Tier-2 startup prefix and may
    // invoke the generated coordinator.  Seed every route with its four-state
    // body before that call; replace it with the proven boundary selection
    // only after the drain reaches quiescence.
    builder.setInsertionPoint(prepare);
    LLVM::StoreOp::create(
        builder, route.twoState.getLoc(),
        LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                  route.fourState.getSymName()),
        LLVM::AddressOfOp::create(builder, route.twoState.getLoc(), pointer,
                                  route.globalName),
        8);
    builder.setInsertionPointAfter(prepare);
    Location location = route.twoState.getLoc();
    Value unknown = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_state_unknown");
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
        anyUnknown =
            LLVM::OrOp::create(builder, location, anyUnknown, bits);
      }
    }
    Value known = encoded.empty()
                      ? detail::llvmConstant(builder, location,
                                             builder.getI1Type(),
                                             route.independentEntry)
                      : LLVM::ICmpOp::create(
                            builder, location, LLVM::ICmpPredicate::eq,
                            anyUnknown,
                            detail::llvmConstant(builder, location, i8, 0));
    Value selected = LLVM::SelectOp::create(
        builder, location, known,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  route.twoState.getSymName()),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  route.fourState.getSymName()));
    LLVM::StoreOp::create(
        builder, location, selected,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  route.globalName),
        8);
  }

  llvm::StringMap<Route *> routesByFunction;
  for (Route &route : routes) {
    routesByFunction[route.fourState.getSymName()] = &route;
    routesByFunction[route.twoState.getSymName()] = &route;
  }

  // An asynchronous X/Z handoff clears the model-wide promotion latch. Reset
  // every non-vacuous local route in the same generated invalidator so the
  // four-state coordinator cannot retain a stale two-state leaf selection.
  if (LLVM::LLVMFuncOp invalidate =
          module.lookupSymbol<LLVM::LLVMFuncOp>(
              "__obelisk_eval_promotion_invalidate_v1")) {
    SmallVector<LLVM::ReturnOp> returns;
    invalidate.walk(
        [&](LLVM::ReturnOp returnOp) { returns.push_back(returnOp); });
    for (LLVM::ReturnOp returnOp : returns) {
      builder.setInsertionPoint(returnOp);
      for (Route &route : routes)
        LLVM::StoreOp::create(
            builder, returnOp.getLoc(),
            LLVM::AddressOfOp::create(
                builder, returnOp.getLoc(), pointer,
                route.fourState.getSymName()),
            LLVM::AddressOfOp::create(builder, returnOp.getLoc(), pointer,
                                      route.globalName),
            8);
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
        if (route.ranges.size() == 0 && !route.independentEntry)
          continue;
        Location location = returnOp.getLoc();
        Value unknown = LLVM::AddressOfOp::create(
            builder, location, pointer, "__obelisk_state_unknown");
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
              mask &= static_cast<uint8_t>(
                  (uint16_t{1} << (lastBit % 8)) - 1);
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
        Value promote = LLVM::AndOp::create(builder, location, ready,
                                            locallyKnown);
        Value address = LLVM::AddressOfOp::create(
            builder, location, pointer, route.globalName);
        Value existing = LLVM::LoadOp::create(
            builder, location, pointer, address, 8);
        Value selected = LLVM::SelectOp::create(
            builder, location, promote,
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      route.twoState.getSymName()),
            existing);
        LLVM::StoreOp::create(builder, location, selected, address, 8);
      }
    }
  }

  SmallVector<LLVM::CallOp> calls;
  module.walk([&](LLVM::CallOp call) {
    if (call.getCallee() && routesByFunction.contains(*call.getCallee()))
      calls.push_back(call);
  });
  for (LLVM::CallOp call : calls) {
    Route &route = *routesByFunction.lookup(*call.getCallee());
    LLVM::LLVMFuncOp caller = call->getParentOfType<LLVM::LLVMFuncOp>();
    bool selectedVariant =
        caller && caller->hasAttr("obelisk.eval.four_state_source");
    bool promotedClosure =
        caller &&
        (caller.getSymName() ==
             "__obelisk_eval_fast_coordinator_two_state_v1" ||
         caller.getSymName() ==
             "__obelisk_eval_periodic_two_state_coordinator_v1" ||
         caller.getSymName().ends_with(".two_state") || selectedVariant);
    bool locallySelectedRoute =
        caller && caller.getSymName().ends_with(".two_state") &&
        route.independentEntry && !route.ranges.empty();
    if (promotedClosure && !locallySelectedRoute) {
      // The outer coordinator has already established the quiescent
      // owner boundary, including nested raw-capture bodies. Preserve that
      // proof as a direct edge so LLVM can inline across module instances.
      // Empty ordinary helpers retain the four-state ABI because their
      // unknown values may be supplied by the caller.  A checkpoint-capable
      // instance is different: its generated two-state owner wrapper is the
      // quiescent boundary proof, while display/finish remain cold exits from
      // that body.  Preserve that proof through the wrapper-to-instance edge.
      bool checkpointOwnerBoundary =
          caller && caller.getSymName().ends_with(".two_state") &&
          route.ranges.empty();
      bool useTwoState = selectedVariant || !route.ranges.empty() ||
                         route.independentEntry || checkpointOwnerBoundary;
      call.setCallee(useTwoState ? route.twoState.getSymName()
                                : route.fourState.getSymName());
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
        "obelisk.eval.allowed_callees",
        builder.getArrayAttr(
            {FlatSymbolRefAttr::get(context, route.fourState.getSymName()),
             FlatSymbolRefAttr::get(context, route.twoState.getSymName())}));
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
        function->hasAttr("obelisk.eval.four_state_source") ||
        function.getSymName().ends_with(".two_state") ||
        function.getSymName().contains(".__obelisk_two_state");
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
  constexpr StringLiteral fourStateName =
      "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral twoStateName =
      "__obelisk_aot_static_nba_commit_two_state_v1";
  constexpr StringLiteral fastTwoStateName =
      "__obelisk_aot_static_nba_commit_two_state_fast_v1";
  LLVM::LLVMFuncOp source =
      module.lookupSymbol<LLVM::LLVMFuncOp>(fourStateName);
  LLVM::LLVMFuncOp coordinator = module.lookupSymbol<LLVM::LLVMFuncOp>(
      "__obelisk_eval_fast_coordinator_two_state_v1");
  if (!source || !coordinator)
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
  auto fastClone =
      cast<LLVM::LLVMFuncOp>(builder.clone(*clone.getOperation()));
  fastClone.setSymName(fastTwoStateName);
  fastClone->setAttr(
      "passthrough",
      ArrayAttr::get(module.getContext(),
                     {StringAttr::get(module.getContext(), "alwaysinline")}));
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
  coordinator.walk([&](LLVM::CallOp call) {
    if (call.getCallee() && *call.getCallee() == fourStateName &&
        !call->hasAttr("obelisk.eval.keep_four_state_nba"))
      call.setCallee(fastTwoStateName);
  });
  if (LLVM::LLVMFuncOp periodicCoordinator =
          module.lookupSymbol<LLVM::LLVMFuncOp>(
              "__obelisk_eval_periodic_two_state_coordinator_v1"))
    periodicCoordinator.walk([&](LLVM::CallOp call) {
      if (call.getCallee() && *call.getCallee() == fourStateName &&
          call->hasAttr("obelisk.eval.use_fast_two_state_nba")) {
        call.setCallee(fastTwoStateName);
        call->removeAttr("obelisk.eval.use_fast_two_state_nba");
      }
    });
  if (LLVM::LLVMFuncOp hybrid = module.lookupSymbol<LLVM::LLVMFuncOp>(
          "__obelisk_eval_fast_coordinator_hybrid_v1"))
    hybrid.walk([&](LLVM::CallOp call) {
      if (call.getCallee() && *call.getCallee() == fourStateName &&
          call->hasAttr("obelisk.eval.use_fast_two_state_nba")) {
        call.setCallee(fastTwoStateName);
        call->removeAttr("obelisk.eval.use_fast_two_state_nba");
      } else if (call.getCallee() && *call.getCallee() == fourStateName &&
                 call->hasAttr(
                     "obelisk.eval.use_canonical_two_state_nba")) {
        call.setCallee(twoStateName);
        call->removeAttr("obelisk.eval.use_canonical_two_state_nba");
      }
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
