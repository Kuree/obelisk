#include "NativePipeline.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;
namespace obelisk::detail {
LogicalResult NativePipelineAnalysis::planState() {
  if (bytecodeOnly)
    return success();
  vpi = analysis::SimulationVPIAnalysis::compute(metadataDesign);
  // Resolved nets and driver contributions occupy the same canonical native
  // planes as storage.  With no external writer their fixed handles are
  // always safe to address directly; publication and resolution still flow
  // through the ordinary scheduler boundaries. Writable VPI retains guarded
  // net accesses when there are no language observers/overrides. Driver
  // contributions need a whole-resolution clean guard, not a per-root guard:
  // a forced net must retain subsequent unforced driver updates for release.
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
  if (auto mode =
          ::obelisk::schedule::get<::obelisk::schedule::Field::NativeScheduler>(
              module))
    nativeScheduler = mode.getValue();
  evalScheduler = nativeScheduler == schedule::NativeSchedulerMode::Eval;

  return success();
}

LogicalResult NativePipelineAnalysis::planActors() {
  if (bytecodeOnly)
    return success();
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
    if (analysis::isNegativeTimingDelayCommit(function) ||
        aotEligibility.getRuntimeObservedWriterActors().contains(function))
      function->setAttr(runtimePublicationCertificate, UnitAttr::get(context));
  }
  if (module->hasAttr("obelisk.bytecode.image")) {
    auto missingBytecode = [](Operation *function) {
      return !function->hasAttr("obelisk.bytecode.function");
    };
    for (const auto &[function, blocks] : aotEligibility.getBytecodeFragments())
      if (!blocks.empty() &&
          aotEligibility.getActorSlots().contains(function) &&
          missingBytecode(function))
        return function->emitError(
            "native fallback requires retained bytecode");
    for (Operation *function : aotEligibility.getRuntimeObservedWriterActors())
      if (missingBytecode(function))
        return function->emitError(
            "runtime checkpoint requires retained bytecode");
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
  if (nativeScheduler != schedule::NativeSchedulerMode::Generic) {
    bool forcedAOT =
        nativeScheduler == schedule::NativeSchedulerMode::AOT || evalScheduler;
    useAOT = aotEligibility.isEligible() &&
             (forcedAOT || aotEligibility.isAOTCostEffective());
  }
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
  // IEEE 1800-2023 4.4-4.7 requires common region arbitration, not a common
  // executor for unrelated processes. Eval can require a certified static
  // island while leaving testbench strings, real values and task control at
  // their existing runtime boundaries. The actor inventory above and final
  // eval call-closure verification still reject an incomplete hot closure.
  // The legacy AOT executor has no equivalent island contract.
  bool forcedAOT =
      nativeScheduler == schedule::NativeSchedulerMode::AOT || evalScheduler;
  if (forcedAOT && !aotEligibility.isFullyEligible() &&
      !aotEligibility.isForcedHybridEligible() &&
      !(evalScheduler && certifiedStaticSuperstep)) {
    InFlightDiagnostic diagnostic =
        module.emitError("design is ineligible for native AOT scheduling: ");
    if (aotEligibility.getReasons().empty())
      diagnostic << "no statically schedulable process actors";
    else
      llvm::interleaveComma(aotEligibility.getReasons(), diagnostic);
    return failure();
  }
  // The legacy hybrid AOT scheduler must retain generic fanout for its cold
  // coordinator. Only eval has the explicit island ABI and periodic overlap
  // guard needed to execute the residual closure directly. Fully eligible
  // designs retain independent static capabilities even when a focused
  // conversion pipeline did not run the optional superstep planner.
  staticEvalIsland = certifiedStaticSuperstep &&
                     (evalScheduler ||
                      nativeScheduler == schedule::NativeSchedulerMode::Auto) &&
                     !aotEligibility.isFullyEligible();
  closedStaticIsland = aotEligibility.isFullyEligible() || staticEvalIsland;
  cleanSuperstep = certifiedStaticSuperstep && closedStaticIsland;

  return success();
}

LogicalResult NativePipelineAnalysis::planSchedule() {
  if (bytecodeOnly)
    return success();
  SymbolTableCollection planningSymbols;
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
  // 1800-2023 10.6.2 still requires forced writes and release to use the
  // runtime path, including retained continuous values. Dynamic override
  // ownership is not covered by the static root inventory and remains
  // conservative. The separate net/driver authorization above retains its
  // resolution barrier.
  directStaticState = staticSpecialization && vpi.hasComputeGraph() &&
                      !hasDynamicLanguageOverride &&
                      (!stateLayout->directHandles.empty() ||
                       !stateLayout->guardedHandles.empty());
  if (useAOT && closedStaticIsland) {
    staticControl = vpi.hasComputeGraph();
    staticFanoutMetadata = vpi.hasComputeGraph();
    // IEEE 1800-2023 9.4.2, 38.34, 38.36: writes change values, not the
    // elaborated sensitivity graph. Live observation revokes the runtime
    // lease; force/release and deposits retain their publication barriers.
    staticFanout = vpi.hasComputeGraph();
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
  if ((nativeScheduler == schedule::NativeSchedulerMode::Auto ||
       evalScheduler) &&
      metadataDesign) {
    // An unpromoted automatic reference needs a runtime activation frame
    // (IEEE 1800-2023 6.21). Keep its complete owner at a checkpoint before
    // certifying NBA ownership; a helper's local packed temporary must not
    // introduce allocation/load calls into the runtime-free eval closure.
    analysis::HandleDataflowAnalysis provenanceAnalysis(metadataDesign);
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
    DenseMap<Operation *, Operation *> runtimeStateOperations;
    DenseMap<Operation *, std::unique_ptr<analysis::NativeHotPathReachability>>
        hotPaths;
    auto hotFor =
        [&](sim::SimFuncOp function) -> analysis::NativeHotPathReachability & {
      auto &entry = hotPaths[function];
      if (!entry)
        entry = std::make_unique<analysis::NativeHotPathReachability>(function);
      return *entry;
    };
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
      Operation *runtimeOperation = nullptr;
      SmallVector<sim::SimFuncOp> pending{function};
      llvm::SmallPtrSet<Operation *, 8> visited;
      while (!pending.empty()) {
        sim::SimFuncOp current = pending.pop_back_val();
        if (!visited.insert(current.getOperation()).second)
          continue;
        auto [classification, inserted] =
            runtimeStateFunctions.try_emplace(current.getOperation(), false);
        if (inserted) {
          analysis::StorageWriteAnalysis writeAnalysis(
              current, provenanceAnalysis.analyze(current), false);
          const auto &provenance = writeAnalysis.getHandles().facts;
          auto directDynamicStore = [&](Value destination) {
            auto target = writeAnalysis.lookup(destination);
            if (!writeAnalysis.hasDirectDynamicSelection(destination) ||
                target.rootWidth > 64 || target.width > 64)
              return false;
            Type element =
                cast<sim::RefType>(destination.getType()).getElementType();
            if (!isa_and_nonnull<IntegerType, sim::LogicType>(
                    sim::getPackedScalarType(element)))
              return false;
            auto handle = stateLayout->storage.find(target.descriptor);
            obelisk_rt_stable_handle_v1 decoded{};
            return handle != stateLayout->storage.end() &&
                   obelisk_rt_stable_handle_decode(handle->second, &decoded) &&
                   decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
                   stateLayout->directHandles.contains(decoded.id);
          };
          auto runtimeStore = [&](Value destination,
                                  bool requireStaticAccess = true,
                                  bool requireStaticNBA = false,
                                  bool allowDirectDynamicStore = false) {
            auto found = provenance.find(destination);
            if (found == provenance.end() || !found->second.descriptor ||
                (requireStaticAccess && found->second.dynamic &&
                 !(allowDirectDynamicStore && directDynamicStore(destination))))
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
            if (!hotFor(current).canExecute(operation))
              return;
            bool requiresRuntime = false;
            // Dynamic stores without a direct-lowering certificate use
            // the bounded, override-aware runtime plane API. Preserve the
            // complete source activation at a checkpoint (IEEE
            // 1800-2023 4.6(a), 9.4.2, 11.5.1), including its transition
            // publications. Managed heap access uses the native actor's GC lane
            // and root scope. Keep that compiled activation at a runtime
            // service checkpoint; it is not a bytecode or coroutine
            // requirement. This also preserves notifications from managed field
            // stores.
            if (isa<sim::SimRefAllocOp, sim::SimClassAllocOp,
                    sim::SimClassCopyOp, sim::SimManagedLoadOp,
                    sim::SimManagedStoreOp, sim::SimManagedBitsDynStoreOp,
                    sim::SimClassDirectCallOp, sim::SimClassVirtualCallOp>(
                    operation))
              requiresRuntime = true;
            else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
              requiresRuntime =
                  isa<sim::StringType>(store.getValue().getType()) ||
                  runtimeStore(store.getReference(), true, false, true);
            else if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation))
              requiresRuntime = runtimeStore(copy.getDestination());
            else if (auto store = dyn_cast<sim::SimNetWriteOp>(operation))
              requiresRuntime = runtimeStore(store.getNet());
            else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation))
              requiresRuntime = runtimeStore(drive.getDriver());
            else if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
              // LRM 4.6(b), 10.4.2: roots excluded from static NBA
              // specialization (including delayed/immediate mixtures)
              // keep their complete owner at the ordered runtime queue.
              requiresRuntime =
                  staticEvalIsland &&
                  runtimeStore(enqueue.getDestination(), false, true);
            }
            classification->second |= requiresRuntime;
            if (requiresRuntime && detailedTiming)
              runtimeStateOperations.try_emplace(current, operation);
          });
        }
        runtimeLocal |= classification->second;
        if (!runtimeOperation)
          runtimeOperation = runtimeStateOperations.lookup(current);
        current.walk([&](sim::SimCallOp call) {
          if (!hotFor(current).canExecute(call))
            return;
          if (sim::SimFuncOp callee =
                  planningSymbols.lookupSymbolIn<sim::SimFuncOp>(
                      metadataDesign, call.getCalleeAttr()))
            pending.push_back(callee);
        });
      }
      if (runtimeLocal) {
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalCheckpointOnly>(
            function, UnitAttr::get(module.getContext()));
        if (detailedTiming) {
          llvm::errs() << "obelisk eval checkpoint admission: actor="
                       << *aotActorSlotFor(actor)
                       << " function=" << actor.getSymName()
                       << " body=" << function.getSymName()
                       << " reason=runtime-state-access";
          if (runtimeOperation)
            llvm::errs() << " operation=" << runtimeOperation->getName()
                         << " source=" << runtimeOperation->getLoc();
          llvm::errs() << '\n';
        }
      }
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
        !(flags & (OBELISK_RT_EXECUTION_DPI_EXPORTS |
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
    // Merge-safe roots use compiler-owned accumulators or fixed clocked
    // slots visible to that barrier. Width alone does not require scheduling
    // an event. Observable update sequences retain their ordering boundary.
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
    SmallVector<bool> clockedBodies;
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalRuntimeCalendar>(module)) {
      auto clocks = buildNativePeriodicClockPlan(
          module, *stateLayout, aotEligibility.getActorSlots());
      if (failed(clocks))
        return failure();
      auto aliases = buildNativePeriodicAliasPlan(
          module, *stateLayout, aotEligibility.getActorSlots(), *clocks);
      if (failed(aliases))
        return failure();
      stateLayout->clockFacts = buildNativeClockInferencePlan(
          module, *stateLayout, aotEligibility.getActorSlots(), *clocks);
    }
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
            unsigned suspensions = 0;
            bool edgeActivation = false;
            actor.walk([&](Operation *operation) {
              if (!sim::isSuspensionOp(operation))
                return;
              ++suspensions;
              edgeActivation |= isa<sim::SimSuspendEdgeOp>(operation);
            });
            bool periodicIngress = false;
            bool nonPeriodicIngress = false;
            for (const auto &entry : staticFanoutPlan.entries)
              if (entry.actor_slot == *aotActorSlotFor(actor)) {
                auto bound =
                    llvm::find_if(stateLayout->bounds, [&](const auto &b) {
                      return b.handleID == entry.static_state;
                    });
                bool periodic =
                    bound != stateLayout->bounds.end() &&
                    entry.bit_width == 1 &&
                    stateLayout->hasClockTickBound(
                        entry.static_state, bound->offset + entry.low_bit);
                periodicIngress |= periodic;
                nonPeriodicIngress |= !periodic;
              }
            clockedBodies.push_back(suspensions == 1 && edgeActivation &&
                                    periodicIngress && !nonPeriodicIngress);
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
          enqueue.getDelay() || site.getTiming() || !function ||
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
      bool supported = root != staticNBAPlan.siteRoots.end() &&
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
              bool sharedCalendar = ::obelisk::schedule::has<
                  ::obelisk::schedule::Field::EvalRuntimeCalendar>(module);
              bool fixedSlots = clockedBodies[index] &&
                                staticNBAPlan.mergeSafeRoots[root] &&
                                (generatedOrigins[root].size() == 1 ||
                                 staticNBAPlan.independentSiteWrites[root]);
              return !orderedRootClosed[root] || !queuePayloadSupported[root] ||
                     (sharedCalendar && !fixedSlots);
            });
        if (!runtimeOwner)
          continue;
        if (detailedTiming &&
            !::obelisk::schedule::has<evalRuntimeNBARequiredAttr>(function)) {
          llvm::errs() << "obelisk eval checkpoint admission: body="
                       << function.getSymName()
                       << " reason=runtime-nba-owner ordered="
                       << bodyNeedsOrderedNBA[index]
                       << " global-queue-closed=" << everySiteGenerated
                       << " clocked=" << clockedBodies[index] << " wide-roots=";
          for (uint32_t root : bodyWideRoots[index])
            llvm::errs()
                << root << ":" << staticNBAPlan.roots[root].bit_width << ":"
                << static_cast<bool>(orderedRootClosed[root]) << ":"
                << static_cast<bool>(queuePayloadSupported[root]) << ":merge="
                << static_cast<bool>(staticNBAPlan.mergeSafeRoots[root])
                << ":independent="
                << static_cast<bool>(staticNBAPlan.independentSiteWrites[root])
                << ",";
          llvm::errs() << '\n';
        }
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
    materializeNBAAccumulators = true;
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
    auto finiteInitialBootstrap =
        [&](sim::SimFuncOp actor, uint32_t continuation) -> FailureOr<bool> {
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
    stateLayout->clockFacts = buildNativeClockInferencePlan(
        module, *stateLayout, aotEligibility.getActorSlots(), periodicClocks);
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

  return success();
}

LogicalResult NativePipelineAnalysis::planOwnership() {
  if (bytecodeOnly)
    return success();

  // All direct bodies and wrappers exist now. Ownership selection only changes
  // their metadata, so share indexes throughout the selected call closures.
  SymbolTableCollection evalSymbols;

  // Resolve typed graph-fusion membership before eval ownership.  Fusion may
  // replace several source actor continuations with one outlined
  // module-instance body, so the source-owner set must be expanded while the
  // current compute graph and its fusion certificate are both available.
  aotFusionGroups = std::move(preLowerFusionOwners);
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
    // group. ExecutableNodes referenced by that coordinator may retain the same
    // group provenance, but expanding each helper to all physical source owners
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
  if (detailedTiming && succeeded(directFragments)) {
    unsigned tier1 = 0, tier2 = 0, checkpoints = 0;
    for (const NativeDirectFragment &direct : *directFragments) {
      if (direct.runtimeCheckpoint)
        ++checkpoints;
      else if (direct.tier2Convergence)
        ++tier2;
      else
        ++tier1;
    }
    llvm::errs() << "obelisk eval executor inventory: tier1=" << tier1
                 << " tier2=" << tier2 << " runtime_checkpoints=" << checkpoints
                 << '\n';
  }
  markTiming("eval ownership and graph planning");

  return success();
}

LogicalResult NativePipelineAnalysis::resolveEval() {
  if (bytecodeOnly)
    return success();
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
    executableNodes.reserve(rankedAOTNodes.size());
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      executableNodes.push_back({slot, continuation, fusionGroup});
    }
    rootSlotZero =
        llvm::any_of(aotEligibility.getActorSlots(), [](const auto &entry) {
          auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
          return function &&
                 function.getEntryKind() == sim::EntryKind::RootInitializer &&
                 entry.second == 0;
        });
    if (evalScheduler) {
      resolvedEval = resolveNativeEvalPlan(
          module, executableNodes, *stateLayout, staticNBAPlan,
          staticFanoutPlan, *directFragments, evalOwnership,
          threeTierPlan.sourceGraph, periodicClocks, periodicAliases);
      if (failed(resolvedEval))
        return failure();
    }
  }

  return success();
}

LogicalResult NativePipelineAnalysis::planExecutableNodes() {
  if (bytecodeOnly)
    return success();
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
      rankedAOTNodes.emplace_back(schedule.initialRank, *schedule.actorSlot, 0,
                                  UINT32_MAX);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        rankedAOTNodes.emplace_back(
            scheduleRanks->getBlockRank(suspension.continuation).value_or(0),
            *schedule.actorSlot, suspension.continuationID,
            fusionGroupFor(*schedule.actorSlot, suspension.continuationID));
    }
    processSchedules.insert({entry.first, std::move(schedule)});
  }

  return success();
}

std::optional<uint32_t>
NativePipelineAnalysis::aotActorSlotFor(sim::SimFuncOp actor) const {
  IntegerAttr codeUnit = actor ? actor.getCodeUnitIdAttr() : IntegerAttr{};
  if (!codeUnit)
    return std::nullopt;
  auto found = aotActorSlotsByCodeUnit.find(codeUnit.getUInt());
  if (found == aotActorSlotsByCodeUnit.end())
    return std::nullopt;
  return found->second;
}
uint32_t NativePipelineAnalysis::fusionGroupFor(uint32_t slot,
                                                uint32_t continuation) const {
  auto found = aotFusionGroups.find({slot, continuation});
  return found == aotFusionGroups.end() ? UINT32_MAX : found->second;
}
} // namespace obelisk::detail

namespace obelisk {
#define GEN_PASS_DEF_PLANNATIVEEXECUTABLENODESPASS
#define GEN_PASS_DEF_PLANNATIVESTATEPASS
#define GEN_PASS_DEF_PLANNATIVEACTORSPASS
#define GEN_PASS_DEF_PLANNATIVESCHEDULEPASS
#define GEN_PASS_DEF_PLANNATIVEEVALOWNERSHIPPASS
#define GEN_PASS_DEF_RESOLVENATIVEEVALSCHEDULEPASS
#include "obelisk/Conversion/Passes.h.inc"
namespace {
class PlanNativeExecutableNodesPass final
    : public impl::PlanNativeExecutableNodesPassBase<
          PlanNativeExecutableNodesPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-plan-native-executable-nodes requires "
                               "the native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Ownership) {
      getOperation().emitError("schedule-plan-native-executable-nodes requires "
                               "native pipeline phase Ownership");
      return signalPassFailure();
    }
    if (failed(state.planExecutableNodes()))
      return signalPassFailure();
    state.stage = Analysis::Stage::ExecutableNodes;
    markAnalysesPreserved<Analysis>();
  }
};

class PlanNativeStatePass final
    : public impl::PlanNativeStatePassBase<PlanNativeStatePass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-plan-native-state requires the native "
                               "preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Inputs) {
      getOperation().emitError(
          "schedule-plan-native-state requires native pipeline phase Inputs");
      return signalPassFailure();
    }
    if (failed(state.planState()))
      return signalPassFailure();
    state.stage = Analysis::Stage::State;
    markAnalysesPreserved<Analysis>();
  }
};
class PlanNativeActorsPass final
    : public impl::PlanNativeActorsPassBase<PlanNativeActorsPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-plan-native-actors requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Frames) {
      getOperation().emitError(
          "schedule-plan-native-actors requires native pipeline phase Frames");
      return signalPassFailure();
    }
    if (failed(state.planActors()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Actors;
    markAnalysesPreserved<Analysis>();
  }
};
class PlanNativeSchedulePass final
    : public impl::PlanNativeSchedulePassBase<PlanNativeSchedulePass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("convert-simulation-to-native-schedule requires "
                               "the native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Captures) {
      getOperation().emitError("convert-simulation-to-native-schedule requires "
                               "native pipeline phase Captures");
      return signalPassFailure();
    }
    if (failed(state.planSchedule()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Schedule;
    markAnalysesPreserved<Analysis>();
  }
};
class PlanNativeEvalOwnershipPass final
    : public impl::PlanNativeEvalOwnershipPassBase<
          PlanNativeEvalOwnershipPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-plan-native-eval-ownership requires "
                               "the native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Fragments) {
      getOperation().emitError("schedule-plan-native-eval-ownership requires "
                               "native pipeline phase Fragments");
      return signalPassFailure();
    }
    if (failed(state.planOwnership()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Ownership;
    markAnalysesPreserved<Analysis>();
  }
};
class ResolveNativeEvalSchedulePass final
    : public impl::ResolveNativeEvalSchedulePassBase<
          ResolveNativeEvalSchedulePass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-resolve-native-eval requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::ExecutableNodes) {
      getOperation().emitError("schedule-resolve-native-eval requires native "
                               "pipeline phase ExecutableNodes");
      return signalPassFailure();
    }
    if (failed(state.resolveEval()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Resolved;
    markAnalysesPreserved<Analysis>();
  }
};
} // namespace
} // namespace obelisk
