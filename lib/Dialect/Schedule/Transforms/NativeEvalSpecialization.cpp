// Native activation specialization preserves canonical fallback bodies.
#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "mlir/Transforms/RegionUtils.h"
#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/OutputItemFlags.h"
#include "obelisk/Runtime/StableHandle.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
using namespace mlir;
namespace obelisk::detail {
namespace {
// A private generated activation may print snapshots without scheduling a
// process or consulting canonical design state. Keep dynamic formats, managed
// values, user file channels and strength queries on the checkpoint route.
bool isDirectOutput(sim::SimDisplayOp display) {
  auto descriptor =
      display.getDescriptor().getDefiningOp<arith::ConstantIntOp>();
  if (!descriptor ||
      (uint32_t(descriptor.value()) != 1 &&
       uint32_t(descriptor.value()) != 0x80000002u) ||
      !display.getScopeAttr())
    return false;
  constexpr uint32_t allowed =
      OBELISK_RT_OUTPUT_ITEM_SIGNED | OBELISK_RT_OUTPUT_ITEM_OMITTED |
      OBELISK_RT_OUTPUT_ITEM_REAL | OBELISK_RT_OUTPUT_ITEM_NET;
  for (int32_t flags : display.getItemFlags())
    if (uint32_t(flags) & ~allowed)
      return false;
  for (Value item : display.getItems()) {
    if (isa<sim::BytesType>(item.getType())) {
      auto literal = item.getDefiningOp<sim::SimBytesConstantOp>();
      if (!literal)
        return false;
      // Over-accept format modifiers here only to reject possible strength
      // queries conservatively. The formatter still validates the syntax.
      // Escaped %% and literal text containing 'v' do not query strengths.
      StringRef format = literal.getValue();
      while (!format.empty()) {
        size_t percent = format.find('%');
        if (percent == StringRef::npos)
          break;
        format = format.drop_front(percent + 1);
        if (format.consume_front("%"))
          continue;
        format = format.ltrim("0123456789-.");
        if (format.starts_with_insensitive("v"))
          return false;
        if (!format.empty())
          format = format.drop_front();
      }
    } else if (!isa<IntegerType, Float64Type, sim::LogicType, sim::NetType>(
                   item.getType())) {
      return false;
    }
  }
  return true;
}

bool isDirectOutput(Operation *operation) {
  auto display = dyn_cast<sim::SimDisplayOp>(operation);
  return display && isDirectOutput(display);
}

} // namespace
/// Preserve the compact-NBA conversion proof on the operation that consumes
/// it. Function and NBA conversion patterns may run in either order, so the
/// NBA lowering must not depend on its parent function still being present.
void annotateCompactNBAMetadata(ModuleOp module) {
  MLIRContext *context = module.getContext();
  module.walk([&](sim::SimFuncOp function) {
    if (!::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalSelectedTwoState>(function))
      return;
    function.walk([&](sim::SimNBAEnqueueOp nba) {
      ::obelisk::schedule::set<schedule::metadata::evalCompactNBAMetadata>(
          nba, UnitAttr::get(context));
    });
  });
}

static void eraseEvalDiscardableStores(sim::SimFuncOp function) {
  SmallVector<sim::SimRefStoreOp> stores;
  function.walk([&](sim::SimRefStoreOp store) {
    if (::obelisk::schedule::has<schedule::metadata::evalDiscardableStore>(
            store))
      stores.push_back(store);
  });
  for (sim::SimRefStoreOp store : stores)
    store.erase();

  bool changed = true;
  while (changed) {
    changed = false;
    SmallVector<Operation *> deadViews;
    function.walk([&](Operation *operation) {
      if (isa<sim::SimContextStorageOp, sim::SimRefExtractOp,
              sim::SimRefDynExtractOp, sim::SimRefSubelementOp,
              sim::SimRefArrayElementOp>(operation) &&
          operation->getNumResults() == 1 && operation->use_empty())
        deadViews.push_back(operation);
    });
    for (Operation *operation : deadViews) {
      operation->erase();
      changed = true;
    }
  }
}

LogicalResult materializeEvalTwoStateVariants(
    ModuleOp module, sim::SimDesignOp design,
    const detail::NativeStateLayout &stateLayout, bool enabled,
    const DenseMap<uint64_t, uint32_t> &actorSlots) {
  // MaterializeComputeFusion may have prepared dormant-Tier1 helper proofs
  // before late AOT eligibility is known. No success or failure path may leak
  // those pass-only markers into bytecode or LLVM lowering.
  llvm::scope_exit discardPromotionProofs([&] {
    if (!design)
      return;
    design.walk([&](sim::SimRefStoreOp store) {
      ::obelisk::schedule::remove<schedule::metadata::evalDiscardableStore>(
          store);
    });
  });
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

  // Proof propagation only changes attributes and bodies. Keep the symbol
  // inventory across those walks and maintain it for every generated or
  // rejected function below, rather than rescanning declarations per call
  // and rebuilding the complete table per variant.
  SymbolTable variantSymbols(design);
  SmallVector<sim::SimFuncOp> roots;
  llvm::SmallPtrSet<Operation *, 16> rootSet;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    IntegerAttr codeUnit = function.getCodeUnitIdAttr();
    if (!codeUnit || !actorSlots.contains(codeUnit.getUInt()))
      continue;
    auto body = ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
        function);
    if (!body)
      continue;
    sim::SimFuncOp target =
        variantSymbols.lookup<sim::SimFuncOp>(body.getValue());
    // Observer entry points are invoked independently by the runtime, outside
    // the generated eval coordinator's Tier-2 handoff. Keep their canonical
    // four-state body instead of manufacturing a coordinator-owned route that
    // has no valid initialization boundary.
    if (!target ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
            target) ||
        ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalInductiveTwoState>(target) ||
        target.getEntryKind() == sim::EntryKind::Observer)
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
            variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
        if (!callee || callee.isExternal())
          return;
        closure.push_back(callee);
      });
    }
    for (sim::SimFuncOp function : closure)
      if (sourceSet.insert(function.getOperation()).second)
        sources.push_back(function);

    // Delayed-net drives own calendar and charge-storage behavior in the
    // runtime. Pass-connected nets likewise require component-wide resolution
    // after every publication (IEEE 1800-2017 28.8 and 28.13). Neither can
    // execute inside the closed Tier-1 evaluator, whose calls must be
    // scheduler-free. Reject the complete owner transitively while the
    // Simulation call graph is still available so Auto can retain the
    // ordinary Tier-2/runtime route instead of discovering the runtime call
    // only after irreversible LLVM conversion.
    bool hasRuntimeDriverResolution = false;
    for (sim::SimFuncOp function : closure)
      function.walk([&](Operation *operation) {
        hasRuntimeDriverResolution |=
            isa<sim::SimDriverDriveDelayedNetOp>(operation) ||
            (stateLayout.hasPassSwitch &&
             isa<sim::SimDriverDriveOp, sim::SimDriverDriveInertialOp,
                 sim::SimDriverDriveInertialPathOp,
                 sim::SimDriverDriveInertialStrengthPairOp,
                 sim::SimDriverDriveInertialPathStrengthPairOp,
                 sim::SimDriverDriveChangedOp>(operation));
      });
    if (hasRuntimeDriverResolution)
      ::obelisk::schedule::set<
          schedule::metadata::evalUnsupportedCheckpointOwner>(
          root, StringAttr::get(module.getContext(), root.getSymName()));
  }

  if (sources.empty())
    return success();
  // Only mark private activation roots, not shared canonical helpers or
  // independently scheduled monitor/observer callbacks.
  for (sim::SimFuncOp root : roots)
    root.walk([&](sim::SimDisplayOp display) {
      if (isDirectOutput(display))
        ::obelisk::schedule::set<schedule::Field::EvalDirectOutput>(
            display, UnitAttr::get(module.getContext()));
    });
  llvm::SmallPtrSet<Operation *, 32> variantEligibleSources;
  llvm::SmallPtrSet<Operation *, 16> knownOnlyPathSources;
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
        [&](Value handle, const analysis::HandleFacts &provenance,
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
          found->second.resource == schedule::ComputeResourceKind::Storage
              ? &stateLayout.storage
          : found->second.resource == schedule::ComputeResourceKind::Net
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
      analysis::HandleFacts provenance = analysis::deriveHandleFacts(source);
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
        if (isDirectOutput(operation))
          return;
        if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
                sim::SimProgramExitOp, sim::SimErrorOp, sim::SimStatusCheckOp,
                sim::SimDisplayOp, sim::SimSampledReadOp,
                sim::SimSampledHistoryOp>(operation))
          coldCheckpointBlocks.insert(operation->getBlock());
      });
      source.walk([&](Operation *operation) {
        if (isDirectOutput(operation))
          return;
        // Pass-connected nets require component-wide resolution after every
        // driver publication (IEEE 1800-2017 28.8 and 28.13). The native
        // lowering performs that resolution through the scheduler runtime,
        // so this operation cannot enter a runtime-free eval closure.
        if (stateLayout.hasPassSwitch &&
            isa<sim::SimDriverDriveOp, sim::SimDriverDriveInertialOp,
                sim::SimDriverDriveInertialPathOp,
                sim::SimDriverDriveInertialStrengthPairOp,
                sim::SimDriverDriveInertialPathStrengthPairOp,
                sim::SimDriverDriveDelayedNetOp, sim::SimDriverDriveChangedOp>(
                operation)) {
          runtimeFree = false;
          checkpointSafe = false;
        }
        if (isa_and_nonnull<runtime::ObeliskRuntimeDialect>(
                operation->getDialect())) {
          preserving = false;
          runtimeFree = false;
          checkpointSafe = false;
          return;
        }
        if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
                sim::SimProgramExitOp, sim::SimErrorOp, sim::SimStatusCheckOp,
                sim::SimDisplayOp, sim::SimSampledReadOp,
                sim::SimSampledHistoryOp>(operation)) {
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
                sim::SimFileRewindOp, sim::SimDumpOpenOp,
                sim::SimDumpOpenStringOp, sim::SimDumpTimescaleOp,
                sim::SimDumpVarsOp, sim::SimDumpAllOp, sim::SimDumpControlOp,
                sim::SimDumpLimitOp, sim::SimDumpFlushOp, sim::SimDumpPortsOp,
                sim::SimDumpPortsControlOp>(operation)) {
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
        if (auto write = dyn_cast<sim::SimNetWriteOp>(operation)) {
          auto found = provenance.find(write.getNet());
          if (found != provenance.end() && found->second.descriptor &&
              domains->isInductivelyTwoState(found->second.resource,
                                             *found->second.descriptor))
            (void)selectRange(write.getNet(), provenance, inductiveRanges);
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
                            write.getValue()) &&
                        selectRange(write.getNet(), provenance, localRanges);
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
        if (auto drive = dyn_cast<sim::SimDriverDriveInertialOp>(operation)) {
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
        if (auto drive =
                dyn_cast<sim::SimDriverDriveInertialPathOp>(operation)) {
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
        if (auto pair = dyn_cast<sim::SimDriverDriveInertialStrengthPairOp>(
                operation)) {
          auto inspect = [&](Value driver, Value value) {
            auto found = provenance.find(driver);
            if (found != provenance.end() && found->second.descriptor &&
                domains->isInductivelyTwoState(found->second.resource,
                                               *found->second.descriptor))
              (void)selectRange(driver, provenance, inductiveRanges);
            preserving &=
                knownStateDomains->isTwoStateWithInductiveRoots(value) &&
                selectRange(driver, provenance, localRanges);
          };
          inspect(pair.getLowDriver(), pair.getLowValue());
          inspect(pair.getHighDriver(), pair.getHighValue());
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
              pair.getTransitionValue());
          return;
        }
        if (auto pair = dyn_cast<sim::SimDriverDriveInertialPathStrengthPairOp>(
                operation)) {
          auto inspect = [&](Value driver, Value value) {
            auto found = provenance.find(driver);
            if (found != provenance.end() && found->second.descriptor &&
                domains->isInductivelyTwoState(found->second.resource,
                                               *found->second.descriptor))
              (void)selectRange(driver, provenance, inductiveRanges);
            preserving &=
                knownStateDomains->isTwoStateWithInductiveRoots(value) &&
                selectRange(driver, provenance, localRanges);
          };
          inspect(pair.getLowDriver(), pair.getLowValue());
          inspect(pair.getHighDriver(), pair.getHighValue());
          preserving &= knownStateDomains->isTwoStateWithInductiveRoots(
              pair.getTransitionValue());
          return;
        }
        if (auto drive = dyn_cast<sim::SimDriverDriveDelayedNetOp>(operation)) {
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
              variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
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
      if (!::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalRawCaptures>(source))
        continue;
      llvm::SmallDenseSet<PhysicalRange, 16> closureRanges;
      llvm::SmallDenseSet<PhysicalRange, 16> inductiveClosureRanges;
      SmallVector<sim::SimFuncOp> closure{source};
      llvm::SmallPtrSet<Operation *, 16> seen;
      bool closureKnownPreserving = true;
      bool closureRuntimeFree = true;
      bool closureCheckpointSafe = true;
      bool explicitUnknownNBA = false;
      bool alwaysUnknownNBA = false;
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
        function.walk([&](sim::SimNBAEnqueueOp nba) {
          if (knownStateDomains->getWithInductiveRoots(nba.getValue()).reason ==
              StateDomainReason::UnknownConstant) {
            DominanceInfo dominance(function);
            bool dominatesReturns = true;
            bool hasReturn = false;
            function.walk([&](sim::SimReturnOp returnOp) {
              hasReturn = true;
              dominatesReturns &= dominance.dominates(nba, returnOp);
            });
            alwaysUnknownNBA |= hasReturn && dominatesReturns;
          }
          SmallVector<Value> pending{nba.getValue()};
          llvm::SmallPtrSet<Operation *, 16> examined;
          while (!pending.empty()) {
            Operation *op = pending.pop_back_val().getDefiningOp();
            if (!op || !examined.insert(op).second)
              continue;
            if (auto constant = dyn_cast<sim::SimLogicConstantOp>(op))
              explicitUnknownNBA |= !constant.getUnknown().isZero();
            else if (!isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op))
              llvm::append_range(pending, op->getOperands());
          }
        });
        function.walk([&](sim::SimCallOp call) {
          sim::SimFuncOp callee =
              variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
          if (!callee || callee.isExternal())
            return;
          // Raw-capture eval bodies retain independently selected routes.
          // Ordinary value helpers remain in this body's boundary because
          // their direct logic results cannot be routed independently.
          if (callee != source &&
              ::obelisk::schedule::has<
                  ::obelisk::schedule::Field::EvalRawCaptures>(callee))
            return;
          closure.push_back(callee);
        });
      }
      for (BlockArgument argument : source.getBody().front().getArguments())
        if (detail::containsLogic(argument.getType()))
          closureKnownPreserving &= domains->isTwoState(argument);
      if (!closureRuntimeFree && !closureCheckpointSafe)
        continue;
      // A value-domain predicate cannot admit an activation whose unknown
      // NBA payload dominates every return. Keep its four-state executor;
      // path-local predicates remain useful for conditional X/Z writes.
      if (alwaysUnknownNBA)
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
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalLocalPromotionRanges>(
          source, DenseI64ArrayAttr::get(module.getContext(), encoded));
      if (closureKnownPreserving) {
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalConditionallyTwoState>(
            source, UnitAttr::get(module.getContext()));
      }
      // A pure activation can explicitly stage X/Z and read that register
      // on another. Its inductive ranges alone cannot guard the read after
      // the X/Z commit. Use the same generated CFG predicate as checkpoint
      // owners, so only reads reached by this activation constrain promotion.
      bool knownOnlyPath =
          closureRuntimeFree && !closureKnownPreserving && explicitUnknownNBA;
      if (!closureRuntimeFree || knownOnlyPath)
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
            source, UnitAttr::get(module.getContext()));
      if (knownOnlyPath)
        knownOnlyPathSources.insert(source.getOperation());
      if (!closureRuntimeFree && closureKnownPreserving)
        ::obelisk::schedule::set<
            schedule::metadata::evalPathGuardedKnownPreserving>(
            source, UnitAttr::get(module.getContext()));
      if (closureKnownPreserving || !orderedRanges.empty() ||
          !closureRuntimeFree || knownOnlyPath)
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
              variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
          if (callee &&
              ::obelisk::schedule::has<
                  ::obelisk::schedule::Field::EvalRawCaptures>(callee) &&
              !routeEligibleSources.contains(callee.getOperation()))
            closed = false;
        });
        if (!closed)
          rejected.push_back(operation);
      }
      for (Operation *operation : rejected) {
        removed |= routeEligibleSources.erase(operation);
        ::obelisk::schedule::remove<
            ::obelisk::schedule::Field::EvalConditionallyTwoState>(operation);
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
      auto encoded = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalLocalPromotionRanges>(source);
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
              variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
          if (!callee || !routeEligibleSources.contains(callee.getOperation()))
            return;
          // A checkpoint callee owns its own guarded route and promotion
          // closure. Pulling its dormant-path ranges into every caller would
          // couple otherwise independent module instances and prevent the
          // caller from ever reaching its two-state entry.
          if (::obelisk::schedule::has<
                  ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
                  callee))
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
        if (!::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
                operation))
          selectedRanges.insert({offset, width});
      }
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalLocalPromotionRanges>(
          operation, DenseI64ArrayAttr::get(module.getContext(), encoded));
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
            variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
        if (!callee || callee.isExternal())
          return;
        if (::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalRawCaptures>(callee) &&
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
  // A dry-run predicate may evaluate a value helper only when its complete
  // body is context-free. In particular, an empty effect summary alone is
  // insufficient: reject state reads, foreign calls, allocation and recursive
  // cycles. Such calls need neither a state overlay nor a runtime checkpoint.
  DenseMap<Operation *, bool> pureValueHelpers;
  auto isPureValueHelper = [&](auto &&self, sim::SimFuncOp function) -> bool {
    if (!function || function.isExternal())
      return false;
    auto [cached, inserted] =
        pureValueHelpers.try_emplace(function.getOperation(), false);
    if (!inserted)
      return cached->second;
    bool pure = function
                    .walk([&](Operation *operation) {
                      if (operation == function.getOperation() ||
                          isa<sim::SimReturnOp>(operation))
                        return WalkResult::advance();
                      if (auto call = dyn_cast<sim::SimCallOp>(operation))
                        return self(self, variantSymbols.lookup<sim::SimFuncOp>(
                                              call.getCallee()))
                                   ? WalkResult::advance()
                                   : WalkResult::interrupt();
                      return isMemoryEffectFree(operation)
                                 ? WalkResult::advance()
                                 : WalkResult::interrupt();
                    })
                    .wasInterrupted() == false;
    pureValueHelpers[function.getOperation()] = pure;
    return pure;
  };
  auto materializePathKnownProbe =
      [&](sim::SimFuncOp source, StringRef name, uint64_t codeUnit,
          bool trackKnownState = true) -> FailureOr<sim::SimFuncOp> {
    auto traceRejection = [&](StringRef reason,
                              Operation *operation = nullptr) {
      if (!module->hasAttr("obelisk.debug.native_timing"))
        return;
      llvm::errs() << "obelisk eval probe rejected: " << source.getSymName()
                   << ": " << reason;
      if (operation)
        llvm::errs() << " (" << operation->getName() << ")";
      llvm::errs() << "\n";
    };
    llvm::SmallPtrSet<Block *, 4> coldCheckpointBlocks;
    source.walk([&](Operation *operation) {
      if (isDirectOutput(operation))
        return;
      if (isa<sim::SimDisplayOp, sim::SimFinishOp, sim::SimStopOp,
              sim::SimProgramExitOp, sim::SimFatalOp, sim::SimErrorOp,
              sim::SimStatusCheckOp, sim::SimSampledReadOp,
              sim::SimSampledHistoryOp>(operation))
        coldCheckpointBlocks.insert(operation->getBlock());
    });
    if (coldCheckpointBlocks.contains(&source.getBody().front())) {
      // No speculative state is needed when the activation immediately
      // checkpoints. In particular, do not create entry shadow cells that
      // would be destroyed while replacing this entire block below.
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalCheckpointOnly>(
          source, builder.getUnitAttr());
      return sim::SimFuncOp{};
    }
    // A checkpoint returns to the runtime at the beginning of its block.
    // Its original successors are not probe paths unless another hot edge
    // reaches them. Do not let their local temporaries poison alias proofs.
    llvm::SmallPtrSet<Block *, 32> hotProbeBlocks;
    SmallVector<Block *> hotPending{&source.getBody().front()};
    while (!hotPending.empty()) {
      Block *block = hotPending.pop_back_val();
      if (coldCheckpointBlocks.contains(block) ||
          !hotProbeBlocks.insert(block).second)
        continue;
      llvm::append_range(hotPending, block->getSuccessors());
    }

    // A dry run can discard publications that no subsequent read can observe.
    // Resolve exact physical ranges, not just descriptor names: distinct
    // captures can alias the same storage. Unknown/dynamic references remain
    // conservatively aliasing. No speculative state overlay is needed when
    // all reads are disjoint, or when the publication is terminal.
    auto provenance = analysis::deriveHandleFacts(source);
    using ProbeRange = std::pair<uint64_t, uint64_t>;
    DenseMap<Value, std::optional<ProbeRange>> probeRanges;
    DenseMap<Value, std::optional<ProbeRange>> boundedProbeRanges;
    auto rangeFor = [&](Value reference,
                        bool boundDynamic =
                            false) -> std::optional<ProbeRange> {
      auto &cache = boundDynamic ? boundedProbeRanges : probeRanges;
      auto [cached, inserted] = cache.try_emplace(reference, std::nullopt);
      if (!inserted)
        return cached->second;
      auto found = provenance.find(reference);
      if (found == provenance.end() || !found->second.descriptor ||
          (found->second.dynamic && !boundDynamic))
        return std::nullopt;
      const auto &origin = found->second;
      const auto *handles =
          origin.resource == schedule::ComputeResourceKind::Storage
              ? &stateLayout.storage
          : origin.resource == schedule::ComputeResourceKind::Net
              ? &stateLayout.nets
              : nullptr;
      if (!handles)
        return std::nullopt;
      auto handle = handles->find(*origin.descriptor);
      obelisk_rt_stable_handle_v1 decoded{};
      if (handle == handles->end() ||
          !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return std::nullopt;
      auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &entry) {
        return entry.handleID == decoded.id;
      });
      // An unknown selection within a fixed root cannot alias another root.
      // Use the entire physical allocation only for a may-access bound; it
      // must never be mistaken for an exact cell when forwarding writes.
      if (origin.dynamic && bound != stateLayout.bounds.end())
        return cached->second = ProbeRange{bound->offset, bound->width};
      uint64_t width = origin.width ? origin.width : origin.rootWidth;
      if (!width || bound == stateLayout.bounds.end() ||
          origin.low > bound->width || width > bound->width - origin.low)
        return std::nullopt;
      return cached->second = ProbeRange{bound->offset + origin.low, width};
    };
    SmallVector<std::optional<ProbeRange>> readRanges;
    source.walk([&](Operation *operation) {
      if (!hotProbeBlocks.contains(operation->getBlock()))
        return;
      if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
        readRanges.push_back(rangeFor(load.getReference(), true));
      else if (auto read = dyn_cast<sim::SimNetReadOp>(operation))
        readRanges.push_back(rangeFor(read.getNet(), true));
    });
    llvm::SmallPtrSet<Block *, 32> reachesRead;
    SmallVector<Block *> readWorklist;
    for (Block &block : source.getBody()) {
      if (!hotProbeBlocks.contains(&block))
        continue;
      if (llvm::any_of(block, [](Operation &op) {
            return isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op);
          }))
        readWorklist.push_back(&block);
    }
    while (!readWorklist.empty()) {
      Block *block = readWorklist.pop_back_val();
      if (!hotProbeBlocks.contains(block) || !reachesRead.insert(block).second)
        continue;
      for (Block *predecessor : block->getPredecessors())
        readWorklist.push_back(predecessor);
    }
    llvm::SmallPtrSet<Operation *, 16> terminalStores;
    source.walk([&](sim::SimRefStoreOp store) {
      if (!hotProbeBlocks.contains(store->getBlock()))
        return;
      auto written = rangeFor(store.getReference(), true);
      bool independent =
          written && llvm::all_of(readRanges, [&](auto read) {
            if (!read)
              return false;
            return written->first <= read->first
                       ? written->second <= read->first - written->first
                       : read->second <= written->first - read->first;
          });
      bool readsAfter = false;
      for (Operation *next = store->getNextNode(); next;
           next = next->getNextNode())
        readsAfter |= isa<sim::SimRefLoadOp, sim::SimNetReadOp>(next);
      for (Block *successor : store->getBlock()->getSuccessors())
        readsAfter |= reachesRead.contains(successor);
      if (!readsAfter || independent)
        terminalStores.insert(store.getOperation());
    });

    // For exact captured cells, model blocking writes in private SSA state.
    // The real activation still publishes every store; only its dry run uses
    // this overlay. Fixed slices wholly contained in a cell use packed
    // insert/extract operations; dynamic aliases and mixed state domains are
    // deliberately excluded. Promotion below must eliminate all shadow
    // allocations before the predicate is admitted to the native closure.
    struct ProbeCell {
      ProbeRange range;
      Value reference;
      Type type;
    };
    SmallVector<ProbeCell> cells;
    auto overlaps = [](ProbeRange lhs, ProbeRange rhs) {
      return lhs.first <= rhs.first ? lhs.second > rhs.first - lhs.first
                                    : rhs.second > lhs.first - rhs.first;
    };
    auto contains = [](ProbeRange outer, ProbeRange inner) {
      return inner.first >= outer.first &&
             inner.first - outer.first <= outer.second &&
             inner.second <= outer.second - (inner.first - outer.first);
    };
    source.walk([&](sim::SimRefStoreOp store) {
      if (!hotProbeBlocks.contains(store->getBlock()) ||
          terminalStores.contains(store.getOperation()))
        return;
      Value reference = store.getReference();
      for (unsigned depth = 0; depth != 16; ++depth) {
        if (auto slice = reference.getDefiningOp<sim::SimRefExtractOp>())
          reference = slice.getInput();
        else if (auto field =
                     reference.getDefiningOp<sim::SimRefSubelementOp>())
          reference = field.getInput();
        else
          break;
      }
      auto argument = dyn_cast<BlockArgument>(reference);
      auto storage = reference.getDefiningOp<sim::SimContextStorageOp>();
      auto context = storage ? dyn_cast<BlockArgument>(storage.getContext())
                             : BlockArgument{};
      auto written = rangeFor(reference);
      Type cellType = cast<sim::RefType>(reference.getType()).getElementType();
      bool entryAvailable =
          (argument && argument.getOwner() == &source.getBody().front()) ||
          (context && context.getOwner() == &source.getBody().front());
      if (!entryAvailable || !written || !rangeFor(store.getReference()) ||
          !sim::getPackedWidth(cellType) ||
          llvm::any_of(cells, [&](const ProbeCell &cell) {
            return cell.range == *written;
          }))
        return;
      bool exact = true;
      source.walk([&](Operation *operation) {
        if (!exact || !hotProbeBlocks.contains(operation->getBlock()) ||
            terminalStores.contains(operation))
          return;
        Value reference;
        Type valueType;
        if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
          reference = load.getReference();
          valueType = load.getResult().getType();
        } else if (auto other = dyn_cast<sim::SimRefStoreOp>(operation)) {
          reference = other.getReference();
          valueType = other.getValue().getType();
        } else if (auto read = dyn_cast<sim::SimNetReadOp>(operation)) {
          reference = read.getNet();
          valueType = read.getResult().getType();
        } else {
          return;
        }
        auto accessed = rangeFor(reference, true);
        auto range = rangeFor(reference);
        Type scalar = sim::getPackedScalarType(valueType);
        Type cellScalar = sim::getPackedScalarType(cellType);
        if (!accessed ||
            (overlaps(*written, *accessed) &&
             (!range || !contains(*written, *range) || !scalar ||
              isa<sim::LogicType>(scalar) != isa<sim::LogicType>(cellScalar) ||
              !isa<sim::RefType>(reference.getType()))))
          exact = false;
      });
      if (exact)
        cells.push_back({*written, reference, cellType});
    });
    auto cellFor = [&](Value reference) -> std::optional<unsigned> {
      auto range = rangeFor(reference);
      if (!range)
        return std::nullopt;
      for (auto [index, cell] : llvm::enumerate(cells))
        if (contains(cell.range, *range))
          return index;
      return std::nullopt;
    };

    bool supported = true;
    source.walk([&](Operation *operation) {
      if (!supported)
        return;
      if (operation == source.getOperation())
        return;
      // The probe replaces a cold checkpoint block at its entry and never
      // executes any operation from that block. The Tier-3 callback executes
      // the original block exactly once, including scheduler reads and state
      // publications, so those operations neither require a dry-run overlay
      // nor belong to the generated evaluator's call closure.
      if (!hotProbeBlocks.contains(operation->getBlock()))
        return;
      if (isa<sim::SimCoveragePointHitOp>(operation))
        return;
      if (terminalStores.contains(operation))
        return;
      if (auto store = dyn_cast<sim::SimRefStoreOp>(operation);
          store && cellFor(store.getReference()))
        return;
      if (!netsDirectlyAddressable && isa<sim::SimNetReadOp>(operation)) {
        traceRejection("runtime net read", operation);
        supported = false;
        return;
      }
      if (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveDelayedNetOp, sim::SimDriverDriveChangedOp>(
              operation)) {
        traceRejection("publication may be observed by a later read",
                       operation);
        supported = false;
        return;
      }
      if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimReturnOp,
              sim::SimNBAEnqueueOp, sim::SimDisplayOp, sim::SimFinishOp,
              sim::SimStopOp, sim::SimFatalOp, sim::SimErrorOp,
              sim::SimProgramExitOp, sim::SimTerminationRequestedOp,
              sim::SimStatusCheckOp, cf::BranchOp, cf::CondBranchOp>(operation))
        return;
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        if (isPureValueHelper(
                isPureValueHelper,
                variantSymbols.lookup<sim::SimFuncOp>(call.getCallee())))
          return;
      }
      if (isa<sim::SimCallOp>(operation) || !isMemoryEffectFree(operation)) {
        traceRejection("unsupported effect", operation);
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
    variantSymbols.insert(probe);
    detail::copyNativePartition(source, probe);
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBorrowedCaptures>(
        probe, builder.getUnitAttr());
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalPathKnownPredicate>(
        probe, builder.getUnitAttr());
    if (::obelisk::schedule::has<detail::cleanEvalBodyAttr>(source))
      ::obelisk::schedule::set<detail::cleanEvalBodyAttr>(
          probe, builder.getUnitAttr());
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
    builder.setInsertionPointToStart(&probeEntry);
    SmallVector<sim::SimRefAllocOp> shadowCells;
    SmallVector<sim::SimRefLoadOp> shadowInitializers;
    for (const ProbeCell &cell : cells) {
      // Fixed captures may already have been replaced by context lookups.
      // Recreate this pure handle lookup before the speculative initial load.
      if (auto storage =
              cell.reference.getDefiningOp<sim::SimContextStorageOp>())
        builder.clone(*storage.getOperation(), mapping);
      auto initial = sim::SimRefLoadOp::create(
          builder, source.getLoc(), cell.type, mapping.lookup(cell.reference));
      shadowInitializers.push_back(initial);
      shadowCells.push_back(sim::SimRefAllocOp::create(
          builder, source.getLoc(),
          sim::RefType::get(module.getContext(), cell.type),
          initial.getResult()));
    }
    auto flatten = [&](Value value, Location location) -> Value {
      Type scalar = sim::getPackedScalarType(value.getType());
      if (scalar == value.getType())
        return value;
      return sim::SimPackedFlattenOp::create(builder, location, scalar, value);
    };
    auto unflatten = [&](Value value, Type type, Location location) -> Value {
      if (type == value.getType())
        return value;
      return sim::SimPackedUnflattenOp::create(builder, location, type, value);
    };
    for (Block &sourceBlock : source.getBody()) {
      builder.setInsertionPointToEnd(mapping.lookup(&sourceBlock));
      for (Operation &operation : sourceBlock) {
        // Probes observe control flow without executing source statements.
        // Count a line only in the selected body or its cold callback.
        if (isa<sim::SimCoveragePointHitOp>(operation) ||
            isDirectOutput(&operation) || terminalStores.contains(&operation))
          continue;
        Location location = operation.getLoc();
        if (hotProbeBlocks.contains(&sourceBlock)) {
          if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
            if (auto index = cellFor(load.getReference())) {
              const ProbeCell &cell = cells[*index];
              ProbeRange range = *rangeFor(load.getReference());
              Value value =
                  sim::SimRefLoadOp::create(builder, location, cell.type,
                                            shadowCells[*index].getResult());
              if (range != cell.range || load.getType() != cell.type) {
                value = flatten(value, location);
                Type scalar = sim::getPackedScalarType(load.getType());
                if (range != cell.range) {
                  uint64_t low = range.first - cell.range.first;
                  if (isa<sim::LogicType>(scalar))
                    value = sim::SimLogicExtractOp::create(
                        builder, location, scalar, value,
                        builder.getI64IntegerAttr(low));
                  else {
                    Value offset = arith::ConstantIntOp::create(
                        builder, location, low, 64);
                    value = sim::SimBitsDynExtractOp::create(
                        builder, location, scalar, value, offset);
                  }
                }
                value = unflatten(value, load.getType(), location);
              }
              mapping.map(load.getResult(), value);
              continue;
            }
          } else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
            if (auto index = cellFor(store.getReference())) {
              const ProbeCell &cell = cells[*index];
              ProbeRange range = *rangeFor(store.getReference());
              Value value = mapping.lookup(store.getValue());
              if (range != cell.range || value.getType() != cell.type) {
                value = flatten(value, location);
                if (range != cell.range) {
                  Value previous = sim::SimRefLoadOp::create(
                      builder, location, cell.type,
                      shadowCells[*index].getResult());
                  previous = flatten(previous, location);
                  uint64_t low = range.first - cell.range.first;
                  if (isa<sim::LogicType>(previous.getType()))
                    value = sim::SimLogicInsertOp::create(
                        builder, location, previous.getType(), previous, value,
                        builder.getI64IntegerAttr(low));
                  else {
                    Value offset = arith::ConstantIntOp::create(
                        builder, location, low, 64);
                    value = sim::SimBitsDynInsertOp::create(
                        builder, location, previous.getType(), previous, value,
                        offset);
                  }
                }
                value = unflatten(value, cell.type, location);
              }
              sim::SimRefStoreOp::create(builder, location, value,
                                         shadowCells[*index].getResult());
              continue;
            }
          }
        }
        builder.clone(operation, mapping);
      }
    }

    llvm::SmallPtrSet<Block *, 4> checkpointBlocks;
    llvm::MapVector<Block *, Location> checkpoints;
    probe.walk([&](Operation *operation) {
      if (isa<sim::SimDisplayOp, sim::SimFinishOp, sim::SimStopOp,
              sim::SimProgramExitOp, sim::SimFatalOp, sim::SimErrorOp,
              sim::SimStatusCheckOp, sim::SimSampledReadOp,
              sim::SimSampledHistoryOp>(operation)) {
        Block *block = operation->getBlock();
        checkpoints.try_emplace(block, operation->getLoc());
      }
    });
    for (auto [block, location] : checkpoints) {
      checkpointBlocks.insert(block);
    }

    llvm::SmallPtrSet<Block *, 16> reachable;
    SmallVector<Block *> pending{&probe.getBody().front()};
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (!reachable.insert(block).second || block->empty())
        continue;
      // A checkpoint is replaced by a return below. Do not make its original
      // successors reachable through an edge that the replacement removes.
      if (checkpointBlocks.contains(block))
        continue;
      for (Block *successor : block->getTerminator()->getSuccessors())
        pending.push_back(successor);
    }
    SmallVector<Block *> unreachable;
    for (Block &block : probe.getBody())
      if (!reachable.contains(&block))
        unreachable.push_back(&block);

    // Detach the complete removed subgraph before destroying any operation.
    // A checkpoint can define values used in a now-unreachable successor, and
    // unreachable blocks can reference one another cyclically. Clearing or
    // erasing one block at a time would leave dangling SSA use-list links.
    for (Block *block : checkpointBlocks)
      if (reachable.contains(block))
        block->dropAllReferences();
    for (Block *block : unreachable)
      block->dropAllReferences();
    for (Block *block : unreachable)
      block->erase();
    for (auto [block, location] : checkpoints) {
      if (!reachable.contains(block))
        continue;
      block->clear();
      builder.setInsertionPointToEnd(block);
      Value checkpoint = arith::ConstantOp::create(
          builder, location, builder.getI8Type(), builder.getI8IntegerAttr(2));
      sim::SimReturnOp::create(builder, location, checkpoint);
    }

    if (!shadowCells.empty()) {
      SmallVector<PromotableAllocationOpInterface> allocations;
      for (auto cell : shadowCells)
        allocations.push_back(
            cast<PromotableAllocationOpInterface>(cell.getOperation()));
      DominanceInfo dominance(probe);
      mlir::DataLayout probeLayout = mlir::DataLayout::closest(probe);
      if (failed(tryToPromoteMemorySlots(allocations, builder, probeLayout,
                                         dominance))) {
        traceRejection("shadow cell promotion failed");
        variantSymbols.erase(probe);
        return sim::SimFuncOp{};
      }
      // A cell overwritten on every path does not read canonical state at
      // all. Do not let its unused initial value poison the known-state proof.
      for (auto initial : shadowInitializers)
        if (initial.getResult().use_empty())
          initial.erase();
    }

    // Validate the executable dry-run overlay, not the unpruned source body.
    // A checkpoint block is replaced above by a constant Tier-3 return, so
    // operations used only to prepare that cold leaf (for example the
    // simulation-time read required by a $finish diagnostic) are not part of
    // the generated Tier-1 closure. Rejecting the source before this pruning
    // lets one cold runtime leaf disable the complete periodic eval group.
    bool probeSupported = true;
    probe.walk([&](Operation *operation) {
      if (!probeSupported || operation == probe.getOperation())
        return;
      if (!netsDirectlyAddressable && isa<sim::SimNetReadOp>(operation)) {
        probeSupported = false;
        return;
      }
      if (isa<sim::SimRefStoreOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveDelayedNetOp, sim::SimDriverDriveChangedOp>(
              operation)) {
        probeSupported = false;
        return;
      }
      if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimReturnOp,
              sim::SimNBAEnqueueOp, sim::SimTerminationRequestedOp,
              cf::BranchOp, cf::CondBranchOp>(operation))
        return;
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        if (isPureValueHelper(
                isPureValueHelper,
                variantSymbols.lookup<sim::SimFuncOp>(call.getCallee())))
          return;
      }
      if (isa<sim::SimCallOp>(operation) || !isMemoryEffectFree(operation))
        probeSupported = false;
    });
    if (!probeSupported) {
      traceRejection("unsupported pruned predicate");
      variantSymbols.erase(probe);
      return sim::SimFuncOp{};
    }

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
      traceRejection("all exits are checkpoints");
      // Resume the canonical actor for this activation. Unlike restarting an
      // outlined body, that preserves the selected edge and any NBA prefix.
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalCheckpointOnly>(
          source, builder.getUnitAttr());
      variantSymbols.erase(probe);
      return sim::SimFuncOp{};
    }

    SmallVector<Operation *> publications;
    probe.walk([&](Operation *operation) {
      if (isa<sim::SimNBAEnqueueOp, sim::SimRefStoreOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveDelayedNetOp, sim::SimDriverDriveChangedOp>(
              operation))
        publications.push_back(operation);
    });
    if (!trackKnownState) {
      for (Operation *publication : publications)
        publication->erase();
      SmallVector<sim::SimReturnOp> returns;
      probe.walk([&](sim::SimReturnOp returnOp) {
        if (!checkpointBlocks.contains(returnOp->getBlock()))
          returns.push_back(returnOp);
      });
      for (sim::SimReturnOp returnOp : returns) {
        builder.setInsertionPoint(returnOp);
        Value direct = arith::ConstantOp::create(builder, returnOp.getLoc(),
                                                 builder.getI8Type(),
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
    Value initiallyKnown =
        arith::ConstantOp::create(builder, source.getLoc(), builder.getI1Type(),
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
      SmallVector<std::pair<Operation *, Value>> blockValues;
      for (Operation &operation : block) {
        for (Value result : operation.getResults())
          if (stateLoads.contains(result))
            blockValues.emplace_back(&operation, result);
        if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation))
          blockValues.emplace_back(&operation, nba.getValue());
      }
      for (auto [operation, loaded] : blockValues) {
        // Integer control storage is intrinsically two-state; only packed
        // language values have an unknown plane to inspect.
        if (isa<IntegerType>(loaded.getType()))
          continue;
        std::optional<unsigned> width =
            detail::nativeStateWidth(loaded.getType());
        // A load whose type has no native logic width cannot be probed for
        // known state. That is a limit of this predicate, not an invalid
        // design: decline the owner so it keeps its canonical four-state
        // route instead of failing an otherwise legal compilation.
        if (!width || !detail::containsLogic(loaded.getType())) {
          traceRejection("load has no native logic width",
                         loaded.getDefiningOp());
          variantSymbols.erase(probe);
          return sim::SimFuncOp{};
        }
        if (isa<sim::UnpackedArrayType, sim::UnpackedStructType,
                sim::UnpackedUnionType>(loaded.getType())) {
          traceRejection("unpacked load needs an aggregate knownness predicate",
                         loaded.getDefiningOp());
          variantSymbols.erase(probe);
          return sim::SimFuncOp{};
        }
        Operation *load = operation;
        if (isa<sim::SimNBAEnqueueOp>(operation))
          builder.setInsertionPoint(operation);
        else
          builder.setInsertionPointAfter(operation);
        auto logicType = sim::LogicType::get(module.getContext(), *width);
        Value flattened = loaded;
        if (loaded.getType() != logicType)
          flattened = sim::SimPackedFlattenOp::create(builder, load->getLoc(),
                                                      logicType, loaded);
        Type bitsType = builder.getIntegerType(*width);
        Value bits = sim::SimLogicToBitsOp::create(builder, load->getLoc(),
                                                   bitsType, flattened);
        Value roundTrip = sim::SimLogicFromBitsOp::create(
            builder, load->getLoc(), logicType, bits);
        Value loadKnown = sim::SimLogicCompareOp::create(
            builder, load->getLoc(), builder.getI1Type(),
            sim::CompareKind::CaseEq, flattened, roundTrip);
        knownSoFar = arith::AndIOp::create(builder, load->getLoc(), knownSoFar,
                                           loadKnown);
      }
      outgoingKnown[&block] = knownSoFar;
    }
    for (Operation *publication : publications)
      publication->erase();

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
        cf::CondBranchOp::create(builder, conditional.getLoc(),
                                 conditional.getCondition(),
                                 conditional.getTrueDest(), trueOperands,
                                 conditional.getFalseDest(), falseOperands);
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
      Value twoState = arith::ConstantOp::create(builder, returnOp.getLoc(),
                                                 builder.getI8Type(),
                                                 builder.getI8IntegerAttr(1));
      Value fourState = arith::ConstantOp::create(builder, returnOp.getLoc(),
                                                  builder.getI8Type(),
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
          return variantSymbols.lookup(candidate) != nullptr;
        },
        counter);
    uint64_t codeUnit = allocateCodeUnit();
    uint64_t sourceScope = 0;
    if (auto sourceCodeUnit = source.getCodeUnitIdAttr()) {
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
    variant.setCodeUnitIdAttr(builder.getI64IntegerAttr(codeUnit));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalInductiveTwoState>(
        variant, builder.getUnitAttr());
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFourStateSource>(
        variant,
        FlatSymbolRefAttr::get(module.getContext(), source.getSymName()));
    if (knownOnlyPathSources.contains(source.getOperation()))
      ::obelisk::schedule::set<schedule::Field::EvalInfallible>(
          variant, builder.getUnitAttr());
    variant.walk([&](sim::SimNBAEnqueueOp nba) {
      ::obelisk::schedule::set<schedule::metadata::evalCompactNBAMetadata>(
          nba, UnitAttr::get(module.getContext()));
    });
    SymbolTable::setSymbolVisibility(variant, SymbolTable::Visibility::Private);
    variantSymbols.insert(cloned, design.getBody().front().end());
    ::obelisk::schedule::set<schedule::metadata::evalTwoStateVariant>(
        source, FlatSymbolRefAttr::get(module.getContext(), name));
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
            source)) {
      SmallString<112> probeName;
      (source.getSymName() + ".__obelisk_path_known").toVector(probeName);
      unsigned probeCounter = 0;
      probeName = SymbolTable::generateSymbolName<112>(
          probeName,
          [&](StringRef candidate) {
            return variantSymbols.lookup(candidate) != nullptr;
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
        ::obelisk::schedule::remove<schedule::metadata::evalTwoStateVariant>(
            source);
        ::obelisk::schedule::remove<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
            source);
        ::obelisk::schedule::set<
            schedule::metadata::evalUnsupportedCheckpointOwner>(
            source, builder.getStringAttr(source.getSymName()));
        variantSymbols.erase(variant);
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
              return variantSymbols.lookup(candidate) != nullptr;
            },
            checkpointProbeCounter);
        uint64_t checkpointProbeCodeUnit = allocateCodeUnit();
        FailureOr<sim::SimFuncOp> checkpointProbe = materializePathKnownProbe(
            source, checkpointProbeName, checkpointProbeCodeUnit,
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
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalPathKnownProbe>(
            variant, FlatSymbolRefAttr::get(module.getContext(), probeName));
        ::obelisk::schedule::set<schedule::metadata::evalPathGuardedTwoState>(
            variant, builder.getUnitAttr());
        pathProbeRoutes.push_back(schedule::PathProbeRouteAttr::get(
            builder.getContext(),
            FlatSymbolRefAttr::get(module.getContext(), name),
            FlatSymbolRefAttr::get(module.getContext(), probeName),
            FlatSymbolRefAttr::get(module.getContext(), checkpointProbeName)));
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
      auto sourceRef = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalFourStateSource>(variant);
      sim::SimFuncOp source =
          sourceRef
              ? variantSymbols.lookup<sim::SimFuncOp>(sourceRef.getValue())
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
        ::obelisk::schedule::remove<schedule::metadata::evalTwoStateVariant>(
            source);
        variantNames.erase(source.getSymName());
      }
      if (Operation *declaration =
              variantDeclarations.lookup(variant.getOperation()))
        declaration->erase();
      variantDeclarations.erase(variant.getOperation());
      variantSymbols.erase(variant);
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

  // Four-state Eval bodies and ordinary coroutines initially share value
  // helpers. Later transition materialization replaces runtime publication
  // inside the generated call closure with direct ingress stores, so mutating
  // a shared definition would make the coroutine publish to the wrong queue
  // after external disturbance. Give the four-state Eval roots a private
  // helper graph. The independently generated two-state roots already call
  // their private inductive variants and need no second clone here.
  SmallVector<sim::SimFuncOp> helperSources;
  llvm::SmallPtrSet<Operation *, 16> helperSet;
  SmallVector<sim::SimFuncOp> pending(roots.begin(), roots.end());
  llvm::SmallPtrSet<Operation *, 16> visitedHelpers;
  while (!pending.empty()) {
    sim::SimFuncOp function = pending.pop_back_val();
    if (!visitedHelpers.insert(function.getOperation()).second)
      continue;
    function.walk([&](sim::SimCallOp call) {
      sim::SimFuncOp callee =
          variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
      if (!callee || callee.isExternal() ||
          ::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
              callee))
        return;
      if (helperSet.insert(callee.getOperation()).second)
        helperSources.push_back(callee);
      pending.push_back(callee);
    });
  }
  llvm::SmallPtrSet<Operation *, 16> privateHelperSet;
  for (sim::SimFuncOp helper : helperSources)
    helper.walk([&](Operation *operation) {
      // Every immediate source update below may publish a static scheduler
      // transition when it is lowered.  Transition materialization rewrites
      // that publication to Eval ingress, so keep shared coroutine helpers
      // out of the rewrite closure for all source kinds, not just variables.
      if (isDirectOutput(operation) ||
          isa<sim::SimRefStoreOp, sim::SimNetWriteOp, sim::SimDriverDriveOp,
              sim::SimDriverDriveDelayedNetOp, sim::SimDriverDriveChangedOp>(
              operation))
        privateHelperSet.insert(helper.getOperation());
    });
  bool addedPrivateAncestor;
  do {
    addedPrivateAncestor = false;
    for (sim::SimFuncOp helper : helperSources) {
      if (privateHelperSet.contains(helper.getOperation()))
        continue;
      helper.walk([&](sim::SimCallOp call) {
        sim::SimFuncOp callee =
            variantSymbols.lookup<sim::SimFuncOp>(call.getCallee());
        if (callee && privateHelperSet.contains(callee.getOperation()))
          addedPrivateAncestor |=
              privateHelperSet.insert(helper.getOperation()).second;
      });
    }
  } while (addedPrivateAncestor);
  llvm::StringMap<std::string> privateHelperNames;
  SmallVector<sim::SimFuncOp> privateHelpers;
  for (sim::SimFuncOp source : helperSources) {
    if (!privateHelperSet.contains(source.getOperation()))
      continue;
    builder.setInsertionPointToEnd(&design.getBody().front());
    SmallString<112> base;
    (source.getSymName() + ".__obelisk_eval_private").toVector(base);
    unsigned counter = 0;
    SmallString<112> name = SymbolTable::generateSymbolName<112>(
        base,
        [&](StringRef candidate) {
          return variantSymbols.lookup(candidate) != nullptr;
        },
        counter);
    uint64_t codeUnit = allocateCodeUnit();
    uint64_t sourceScope = 0;
    if (auto sourceCodeUnit = source.getCodeUnitIdAttr()) {
      auto scope = codeUnitScopes.find(sourceCodeUnit.getUInt());
      if (scope == codeUnitScopes.end())
        return source.emitError(
                   "Eval helper source has no code-unit declaration"),
               failure();
      sourceScope = scope->second;
    }
    sim::SimCodeUnitDeclOp::create(
        builder, source.getLoc(), codeUnit, sourceScope,
        sim::EntryKind::Function, builder.getStringAttr(name),
        builder.getStringAttr("private four-state native eval helper"),
        builder.getUnitAttr());
    Operation *detached = source->clone();
    auto clone = cast<sim::SimFuncOp>(detached);
    clone.setSymName(name);
    clone.setCodeUnitIdAttr(builder.getI64IntegerAttr(codeUnit));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalPrivateHelper>(
        clone, builder.getUnitAttr());
    ::obelisk::schedule::remove<schedule::metadata::evalTwoStateVariant>(clone);
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalConditionallyTwoState>(clone);
    ::obelisk::schedule::remove<
        ::obelisk::schedule::Field::EvalLocalPromotionRanges>(clone);
    SymbolTable::setSymbolVisibility(clone, SymbolTable::Visibility::Private);
    variantSymbols.insert(detached, design.getBody().front().end());
    privateHelperNames[source.getSymName()] = name.str().str();
    privateHelpers.push_back(clone);
  }
  auto redirectPrivateHelpers = [&](sim::SimFuncOp function) {
    function.walk([&](sim::SimCallOp call) {
      auto replacement = privateHelperNames.find(call.getCallee());
      if (replacement != privateHelperNames.end())
        call.setCalleeAttr(
            FlatSymbolRefAttr::get(module.getContext(), replacement->second));
    });
  };
  for (sim::SimFuncOp root : roots)
    redirectPrivateHelpers(root);
  for (sim::SimFuncOp helper : privateHelpers)
    redirectPrivateHelpers(helper);

  // Classify helpers without marking their shared canonical definitions.
  // Only private generated copies may bypass monitor bookkeeping and format
  // their supplied snapshots directly, including diagnostics on stderr.
  auto markDirectOutput = [&](sim::SimFuncOp function) {
    function.walk([&](sim::SimDisplayOp display) {
      if (isDirectOutput(display))
        ::obelisk::schedule::set<schedule::Field::EvalDirectOutput>(
            display, builder.getUnitAttr());
    });
  };
  for (sim::SimFuncOp helper : privateHelpers)
    markDirectOutput(helper);
  for (sim::SimFuncOp variant : variants)
    markDirectOutput(variant);

  // Promotion tagged private helper stores before activation cloning. Consume
  // that proof only in the eval-specialized helper graphs; canonical helpers
  // keep publishing for Tier 2/3 and merely lose the transient marker before
  // dialect conversion. Two-state helper variants are a separate generated
  // closure and need the same treatment as four-state private helpers.
  for (sim::SimFuncOp variant : variants)
    eraseEvalDiscardableStores(variant);
  for (sim::SimFuncOp helper : privateHelpers)
    eraseEvalDiscardableStores(helper);
  if (!pathProbeRoutes.empty())
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalPathProbeRoutes>(
        module, builder.getArrayAttr(pathProbeRoutes));
  return success();
}

// A writable interface does not authorize a writer to interleave with the
// runtime-free evaluator. Clone its call closure so the boundary-checked clean
// version can use direct planes without weakening canonical fallback bodies.
void materializeCleanEvalBodies(sim::SimDesignOp design) {
  SmallVector<std::pair<sim::SimFuncOp, sim::SimFuncOp>> roots;
  SmallVector<sim::SimFuncOp> sources;
  DenseSet<Operation *> seen;
  uint64_t nextCodeUnit = 1;
  for (auto declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    nextCodeUnit = std::max(nextCodeUnit, declaration.getId() + 1);
  for (auto actor : design.getBody().front().getOps<sim::SimFuncOp>()) {
    auto body =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(actor);
    auto source = body ? design.lookupSymbol<sim::SimFuncOp>(body.getValue())
                       : sim::SimFuncOp{};
    if (!source || source.getEntryKind() == sim::EntryKind::Observer)
      continue;
    roots.emplace_back(actor, source);
    if (seen.insert(source).second)
      sources.push_back(source);
  }
  for (size_t index = 0; index != sources.size(); ++index) {
    sim::SimFuncOp source = sources[index];
    source.walk([&](sim::SimCallOp call) {
      auto callee = design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
      if (callee && !callee.isExternal() && seen.insert(callee).second)
        sources.push_back(callee);
    });
  }
  DenseMap<Operation *, sim::SimFuncOp> clones;
  OpBuilder builder(design.getContext());
  SymbolTable symbols(design);
  for (sim::SimFuncOp source : sources) {
    auto clone = cast<sim::SimFuncOp>(source->clone());
    clone.setSymName((source.getSymName() + ".__obelisk_clean").str());
    ::obelisk::schedule::set<detail::cleanEvalBodyAttr>(clone,
                                                        builder.getUnitAttr());
    clone.setCodeUnitIdAttr(builder.getI64IntegerAttr(nextCodeUnit));
    symbols.insert(clone, design.getBody().front().end());
    builder.setInsertionPoint(clone);
    uint64_t scope = 0;
    for (auto declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      if (declaration.getId() == source.getCodeUnitId()) {
        scope = declaration.getScopeId();
        break;
      }
    sim::SimCodeUnitDeclOp::create(
        builder, source.getLoc(), nextCodeUnit++, scope,
        sim::EntryKind::Function, builder.getStringAttr(clone.getSymName()),
        builder.getStringAttr("boundary-guarded clean native eval body"),
        builder.getUnitAttr());
    clones[source] = clone;
  }
  for (sim::SimFuncOp source : sources)
    clones.lookup(source).walk([&](sim::SimCallOp call) {
      auto callee = design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
      if (auto clone = clones.lookup(callee))
        call.setCallee(clone.getSymName());
    });
  for (auto [actor, source] : roots)
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBody>(
        actor, FlatSymbolRefAttr::get(clones.lookup(source).getSymNameAttr()));
  // The canonical copies are no longer generated-evaluator roots. Keeping
  // their raw marker would pull guarded runtime loads back into the closure.
  for (sim::SimFuncOp source : sources)
    ::obelisk::schedule::remove<::obelisk::schedule::Field::EvalRawCaptures>(
        source);
}

} // namespace obelisk::detail
