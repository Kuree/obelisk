//===- NativeAOTAnalysis.cpp - Native scheduler eligibility --------------===//

#include "obelisk/Analysis/NativeAOTAnalysis.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <algorithm>
#include <functional>

using namespace mlir;

namespace obelisk::analysis {
namespace {

bool isManagedType(Type type) {
  if (isa<sim::StringType, sim::ClassHandleType, sim::DynamicArrayType,
          sim::QueueType, sim::BoxType, sim::AssocArrayType,
          sim::ReferencePathType, sim::ManagedRefType, sim::ArgumentRefType>(
          type))
    return true;
  if (auto ref = dyn_cast<sim::RefType>(type))
    return isManagedType(ref.getElementType());
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
    return isManagedType(array.getElementType());
  if (auto record = dyn_cast<sim::UnpackedStructType>(type))
    return llvm::any_of(record.getFields(), [](Attribute field) {
      return isManagedType(cast<sim::FieldAttr>(field).getType());
    });
  if (auto record = dyn_cast<sim::UnpackedUnionType>(type))
    return llvm::any_of(record.getFields(), [](Attribute field) {
      return isManagedType(cast<sim::FieldAttr>(field).getType());
    });
  return false;
}

/// Certify the lifecycle CFG emitted for a persistent $monitor/$fmonitor
/// callback. The marker provides compiler provenance; the structural checks
/// ensure this is one bounded actor which either waits for its next argument
/// change or terminates after being replaced.
bool isPersistentMonitorActor(sim::SimFuncOp function) {
  if (!function || !function->hasAttr("obelisk_sim.persistent_monitor") ||
      !function->hasAttr("internal") ||
      SymbolTable::getSymbolVisibility(function) !=
          SymbolTable::Visibility::Private ||
      function.getEntryKind() != sim::EntryKind::Fork ||
      function.getHomeRegion() != sim::EventRegion::Postponed ||
      function.getDomain() != sim::ExecutionDomain::Design ||
      function.getBody().getBlocks().size() != 4)
    return false;

  Block &entry = function.getBody().front();
  Block &dispatch = *std::next(function.getBody().begin());
  Block &body = *std::next(function.getBody().begin(), 2);
  Block &stale = function.getBody().back();
  auto entryBranch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  auto monitor = dispatch.empty()
                     ? sim::SimMonitorCurrentOp{}
                     : dyn_cast<sim::SimMonitorCurrentOp>(dispatch.front());
  auto choose = dyn_cast<cf::CondBranchOp>(dispatch.getTerminator());
  auto terminate = dyn_cast<sim::SimReturnOp>(stale.getTerminator());
  Operation *wait = body.getTerminator();
  bool fixedWait =
      isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp,
          sim::SimSuspendObserveOp, sim::SimSuspendForeverOp>(wait);
  return entryBranch && entryBranch.getDest() == &dispatch && monitor &&
         std::distance(dispatch.begin(), dispatch.end()) == 2 && choose &&
         choose.getCondition() == monitor.getResult() &&
         choose.getTrueDest() == &body && choose.getFalseDest() == &stale &&
         stale.getNumArguments() == 0 &&
         std::distance(stale.begin(), stale.end()) == 1 && terminate &&
         terminate.getOperands().empty() && fixedWait &&
         wait->getNumSuccessors() == 1 && wait->getSuccessor(0) == &dispatch;
}

} // namespace

bool isNegativeTimingDelayCommit(sim::SimFuncOp function) {
  if (!function ||
      !function->hasAttr("obelisk_sim.negative_timing_delay_commit") ||
      !function->hasAttr("internal") ||
      SymbolTable::getSymbolVisibility(function) !=
          SymbolTable::Visibility::Private ||
      function.getEntryKind() != sim::EntryKind::Fork ||
      function.getHomeRegion() != sim::EventRegion::Active ||
      function.getDomain() != sim::ExecutionDomain::Design ||
      function.getNumArguments() != 2 ||
      function.getBody().getBlocks().size() != 2)
    return false;
  Block &entry = function.getBody().front();
  Block &publish = function.getBody().back();
  if (publish.getNumArguments() != 1 ||
      publish.getArgument(0).getType() != entry.getArgument(1).getType())
    return false;
  auto entryIt = entry.begin();
  auto constant = entryIt == entry.end()
                      ? sim::SimTimeConstantOp{}
                      : dyn_cast<sim::SimTimeConstantOp>(&*entryIt++);
  auto delay = entryIt == entry.end()
                   ? sim::SimSuspendDelayOp{}
                   : dyn_cast<sim::SimSuspendDelayOp>(&*entryIt++);
  if (!constant || !delay || entryIt != entry.end() ||
      constant.getValue() == 0 || !delay.getTimingAttr() ||
      delay.getTimingAttr().getKind() != sim::ComputeTimingKind::Calendar ||
      delay.getResumeRegion() != sim::EventRegion::Active ||
      delay.getDelay() != constant.getResult() ||
      delay.getContinuation() != &publish ||
      delay.getContinuationOperands().size() != 1 ||
      delay.getContinuationOperands().front() != entry.getArgument(1))
    return false;
  auto publishIt = publish.begin();
  auto storage = publishIt == publish.end()
                     ? sim::SimContextStorageOp{}
                     : dyn_cast<sim::SimContextStorageOp>(&*publishIt++);
  auto store = publishIt == publish.end()
                   ? sim::SimRefStoreOp{}
                   : dyn_cast<sim::SimRefStoreOp>(&*publishIt++);
  auto terminate = publishIt == publish.end()
                       ? sim::SimReturnOp{}
                       : dyn_cast<sim::SimReturnOp>(&*publishIt++);
  return storage && store && terminate && publishIt == publish.end() &&
         storage.getContext() == entry.getArgument(0) &&
         store.getReference() == storage.getResult() &&
         store.getValue() == publish.getArgument(0) &&
         terminate.getOperands().empty();
}

bool isNegativeTimingDelayMonitorSpawn(sim::SimSpawnOp spawn,
                                       sim::SimFuncOp target) {
  sim::SimFuncOp monitor = spawn->getParentOfType<sim::SimFuncOp>();
  if (!monitor || !target || !isNegativeTimingDelayCommit(target) ||
      !spawn->hasAttr("obelisk_sim.negative_timing_transport_activation") ||
      !monitor->hasAttr("obelisk_sim.negative_timing_delay_monitor") ||
      !monitor->hasAttr("internal") ||
      SymbolTable::getSymbolVisibility(monitor) !=
          SymbolTable::Visibility::Private ||
      monitor.getEntryKind() != sim::EntryKind::Always ||
      monitor.getHomeRegion() != sim::EventRegion::Active ||
      monitor.getDomain() != sim::ExecutionDomain::Design ||
      monitor.getNumArguments() != 2 ||
      monitor.getBody().getBlocks().size() != 3 ||
      spawn.getCallee() != target.getSymName() ||
      !spawn.getResult().use_empty())
    return false;
  Value context = monitor.getBody().front().getArgument(0);
  Value source = monitor.getBody().front().getArgument(1);
  auto capture = monitor.getArgAttrOfType<sim::CaptureKindAttr>(
      1, "obelisk_sim.capture_kind");
  auto descriptor =
      monitor.getArgAttrOfType<IntegerAttr>(1, sim::metadata::descriptorId);
  bool exactSource = (isa<sim::RefType>(source.getType()) && capture &&
                      capture.getValue() == sim::CaptureKind::Storage) ||
                     (isa<sim::NetType>(source.getType()) && capture &&
                      capture.getValue() == sim::CaptureKind::Net);
  if (!exactSource || !descriptor)
    return false;

  Block &entry = monitor.getBody().front();
  Block &wait = *std::next(monitor.getBody().begin());
  Block &changed = monitor.getBody().back();
  auto entryBranch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  auto suspend = dyn_cast<sim::SimSuspendChangeOp>(wait.getTerminator());
  auto back = dyn_cast<cf::BranchOp>(changed.getTerminator());
  if (entry.getNumArguments() != 2 || wait.getNumArguments() != 0 ||
      changed.getNumArguments() != 0 ||
      std::distance(entry.begin(), entry.end()) != 1 ||
      std::distance(wait.begin(), wait.end()) != 1 ||
      std::distance(changed.begin(), changed.end()) != 3 || !entryBranch ||
      entryBranch.getDest() != &wait || !suspend ||
      suspend.getWatched() != source || suspend.getContinuation() != &changed ||
      !suspend.getContinuationOperands().empty() || !back ||
      back.getDest() != &wait || !back.getDestOperands().empty())
    return false;
  Operation &readOperation = changed.front();
  Value current;
  if (auto read = dyn_cast<sim::SimRefLoadOp>(readOperation)) {
    if (read.getReference() != source)
      return false;
    current = read.getResult();
  } else if (auto read = dyn_cast<sim::SimNetReadOp>(readOperation)) {
    if (read.getNet() != source)
      return false;
    current = read.getResult();
  } else {
    return false;
  }
  return &*std::next(changed.begin()) == spawn.getOperation() &&
         spawn.getNumOperands() == 2 && spawn.getOperand(0) == context &&
         spawn.getOperand(1) == current;
}

bool isNegativeTimingDelayMonitor(sim::SimFuncOp function) {
  if (!function || function.getBody().getBlocks().size() != 3)
    return false;
  Block &changed = function.getBody().back();
  if (std::distance(changed.begin(), changed.end()) != 3)
    return false;
  auto spawn = dyn_cast<sim::SimSpawnOp>(&*std::next(changed.begin()));
  sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
  sim::SimFuncOp target =
      design && spawn ? design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee())
                      : sim::SimFuncOp{};
  return spawn && isNegativeTimingDelayMonitorSpawn(spawn, target);
}

bool isRuntimeClockCoordinator(sim::SimFuncOp function) {
  if (!function ||
      SymbolTable::getSymbolVisibility(function) !=
          SymbolTable::Visibility::Private ||
      function.getEntryKind() != sim::EntryKind::Always ||
      function.getHomeRegion() != sim::EventRegion::Observed ||
      function.getDomain() != sim::ExecutionDomain::Design ||
      (!function->hasAttr("obelisk_sim.timing_check_coordinator") &&
       !function->hasAttr("obelisk_sim.multiclock_sequence_coordinator")))
    return false;
  unsigned clockWaits = 0;
  function.walk([&](sim::SimSuspendClockSetOp) { ++clockWaits; });
  return clockWaits == 1;
}

NativeAOTAnalysis NativeAOTAnalysis::compute(ModuleOp module) {
  NativeAOTAnalysis result;
  bool invalidPlan = false;
  bool onlyConcurrentColdBoundaries = true;
  llvm::SmallDenseSet<Operation *> dynamicActors;
  llvm::SmallDenseSet<Operation *> bytecodeActors;
  auto findContainingFunction = [](Operation *operation) {
    auto function = dyn_cast_or_null<sim::SimFuncOp>(operation);
    if (!function && operation)
      function = operation->getParentOfType<sim::SimFuncOp>();
    return function;
  };
  auto isConcurrentColdActor = [&](Operation *operation) {
    auto function = findContainingFunction(operation);
    if (!function)
      return false;
    // IEEE 1800-2017 16.14 multiclock assertions and Clause 31 timing checks
    // both require exact event-cohort ordering. Keep only their coordinator
    // actors as feature-local bytecode islands; surrounding ordinary actors
    // retain the generated AOT plan.
    if (isRuntimeClockCoordinator(function))
      return true;
    if (isNegativeTimingDelayMonitor(function))
      return true;
    if (isNegativeTimingDelayCommit(function))
      return true;
    if (function->hasAttr("obelisk_sim.skew_deadline_helper")) {
      unsigned eventWaits = 0;
      unsigned delayedTriggers = 0;
      unsigned spawns = 0;
      function.walk([&](sim::SimSuspendEventOp) { ++eventWaits; });
      function.walk([&](sim::SimEventTriggerOp trigger) {
        delayedTriggers += trigger.getDelay() && trigger.getNonblocking();
      });
      function.walk([&](sim::SimSpawnOp) { ++spawns; });
      // IEEE 1800-2017 31.4.2/.3 timer checks use one compiler-owned,
      // once-spawned Reactive actor. Keep precisely that bounded helper as a
      // feature-local bytecode island; arbitrary user event loops cannot gain
      // forced-hybrid admission from the marker alone.
      return SymbolTable::getSymbolVisibility(function) ==
                 SymbolTable::Visibility::Private &&
             function->hasAttr("internal") &&
             function.getEntryKind() == sim::EntryKind::Always &&
             function.getHomeRegion() == sim::EventRegion::Reactive &&
             function.getDomain() == sim::ExecutionDomain::Design &&
             eventWaits == 1 && delayedTriggers == 0 && spawns == 0;
    }
    if (function->hasAttr("obelisk_sim.multiclock_sequence_eos_coordinator"))
      return SymbolTable::getSymbolVisibility(function) ==
                 SymbolTable::Visibility::Private &&
             function->hasAttr("internal") &&
             function->hasAttr("obelisk_sim.concurrent_eos_coordinator") &&
             function->hasAttr("obelisk_sim.concurrent_eos_counted") &&
             function->hasAttr("obelisk_sim.detached_controls") &&
             function.getEntryKind() == sim::EntryKind::Final &&
             function.getHomeRegion() == sim::EventRegion::Active &&
             function.getDomain() == sim::ExecutionDomain::Design;
    if (!function->hasAttr("internal"))
      return false;
    return (function->hasAttr("obelisk_sim.concurrent_report") &&
            function->hasAttr("obelisk_sim.detached_controls") &&
            function.getEntryKind() == sim::EntryKind::Fork &&
            function.getHomeRegion() == sim::EventRegion::Reactive) ||
           (function->hasAttr("obelisk_sim.concurrent_cancel") &&
            function->hasAttr("obelisk_sim.detached_controls") &&
            function->hasAttr("obelisk_sim.priority_signal_resume") &&
            function.getEntryKind() == sim::EntryKind::Fork &&
            function.getHomeRegion() == sim::EventRegion::Reactive) ||
           (function->hasAttr("obelisk_sim.concurrent_abort") &&
            function->hasAttr("obelisk_sim.detached_controls") &&
            function->hasAttr("obelisk_sim.priority_signal_resume") &&
            function.getEntryKind() == sim::EntryKind::Fork &&
            function.getHomeRegion() == sim::EventRegion::Reactive) ||
           (function->hasAttr("obelisk_sim.concurrent_cancel_observer") &&
            function->hasAttr("obelisk_sim.detached_controls") &&
            function.getEntryKind() == sim::EntryKind::Observer) ||
           (function->hasAttr("obelisk_sim.concurrent_abort_observer") &&
            function->hasAttr("obelisk_sim.detached_controls") &&
            function.getEntryKind() == sim::EntryKind::Observer);
  };
  auto rejectPlan = [&](StringRef reason) {
    invalidPlan = true;
    onlyConcurrentColdBoundaries = false;
    result.reasons.emplace_back(reason);
  };
  auto requireBytecodeFragment = [&](Operation *operation, StringRef reason,
                                     bool certifiedCold = false) {
    if (!certifiedCold && !isConcurrentColdActor(operation))
      onlyConcurrentColdBoundaries = false;
    result.reasons.emplace_back(reason);
    auto function = operation->getParentOfType<sim::SimFuncOp>();
    if (!function || !operation->getBlock())
      return;
    auto &fragments = result.bytecodeFragments[function.getOperation()];
    if (!llvm::is_contained(fragments, operation->getBlock()))
      fragments.push_back(operation->getBlock());
  };
  auto excludeBytecodeActor = [&](Operation *operation) {
    if (auto function = operation->getParentOfType<sim::SimFuncOp>())
      bytecodeActors.insert(function.getOperation());
  };

  // Backend selection needs to distinguish arbitrary calendar delays from a
  // free-running clock before physical state layout exists.  This is a
  // conservative structural prefilter; native lowering repeats the proof,
  // resolves the exact packed bit, checks all effects, and rejects duplicate
  // drivers before generated run-until is materialized.
  module.walk([&](sim::SimFuncOp function) {
    if (result.periodicClockCandidate || function.isExternal() ||
        function.getBody().empty())
      return;
    SmallVector<sim::SimSuspendDelayOp> delays;
    SmallVector<sim::SimRefLoadOp> loads;
    SmallVector<sim::SimRefStoreOp> stores;
    SmallVector<sim::SimLogicUnaryOp> unaries;
    SmallVector<arith::XOrIOp> xors;
    function.walk([&](Operation *operation) {
      if (auto op = dyn_cast<sim::SimSuspendDelayOp>(operation))
        delays.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefLoadOp>(operation))
        loads.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefStoreOp>(operation))
        stores.push_back(op);
      else if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation))
        unaries.push_back(op);
      else if (auto op = dyn_cast<arith::XOrIOp>(operation))
        xors.push_back(op);
    });
    if (delays.size() != 1 || loads.size() != 1 || stores.size() != 1 ||
        unaries.size() + xors.size() != 1)
      return;
    sim::SimSuspendDelayOp delay = delays.front();
    auto period = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
    if (!period || period.getValue() == 0 || !delay.getTimingAttr() ||
        delay.getTimingAttr().getKind() != sim::ComputeTimingKind::Calendar ||
        !delay.getContinuationOperands().empty())
      return;
    sim::SimRefLoadOp load = loads.front();
    sim::SimRefStoreOp store = stores.front();
    Operation *toggle = unaries.empty() ? xors.front().getOperation()
                                        : unaries.front().getOperation();
    if (store.getValue().getDefiningOp() != toggle ||
        load.getReference() != store.getReference())
      return;
    if (!unaries.empty() &&
        (unaries.front().getKind() != sim::UnaryKind::BitNot ||
         unaries.front().getInput() != load.getResult()))
      return;
    if (!xors.empty()) {
      arith::XOrIOp xorOp = xors.front();
      Value other = xorOp.getLhs() == load.getResult()   ? xorOp.getRhs()
                    : xorOp.getRhs() == load.getResult() ? xorOp.getLhs()
                                                         : Value{};
      auto one = other ? other.getDefiningOp<arith::ConstantOp>() : nullptr;
      auto integer =
          one ? dyn_cast<IntegerAttr>(one.getValue()) : IntegerAttr{};
      if (!integer || integer.getValue().getBitWidth() != 1 ||
          !integer.getValue().isOne())
        return;
    }
    Block *wait = delay->getBlock();
    Block *body = delay.getContinuation();
    auto back = dyn_cast<cf::BranchOp>(body->getTerminator());
    result.periodicClockCandidate = back && back.getDest() == wait &&
                                    wait->getNumSuccessors() == 1 &&
                                    wait->getSuccessor(0) == body;
  });

  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  if (!design || !design.getComputeGraphAttr()) {
    rejectPlan("missing compute-graph metadata");
    return result;
  }
  llvm::StringMap<sim::SimFuncOp> functionsByName;
  for (Operation &operation : design.getBody().front())
    if (auto function = dyn_cast<sim::SimFuncOp>(operation))
      functionsByName.try_emplace(function.getSymName(), function);
  auto lookupFunction = [&](StringRef name) -> sim::SimFuncOp {
    auto found = functionsByName.find(name);
    return found == functionsByName.end() ? sim::SimFuncOp{} : found->second;
  };
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (graph.getVersion() != sim::metadata::schemaVersion)
    rejectPlan("unsupported compute-graph version");
  if (graph.getWorkers() != 1)
    rejectPlan("AOT scheduling requires one worker");
  ArrayAttr nodes = graph.getNodes();
  DenseMap<uint32_t, bool> coldDeferredCommits;
  for (Attribute edgeAttribute : graph.getEdges()) {
    auto edge = dyn_cast<sim::ComputeEdgeAttr>(edgeAttribute);
    if (!edge || edge.getKind() != sim::ComputeEdgeKind::DeferredStage ||
        edge.getSource() >= nodes.size())
      continue;
    auto source = dyn_cast<sim::ComputeFragmentAttr>(nodes[edge.getSource()]);
    sim::SimFuncOp function =
        source ? lookupFunction(source.getFunction().getValue())
               : sim::SimFuncOp{};
    bool cold = function && isConcurrentColdActor(function);
    auto [found, inserted] =
        coldDeferredCommits.try_emplace(edge.getTarget(), cold);
    if (!inserted)
      found->second &= cold;
  }
  DenseMap<Block *, sim::ComputeFragmentAttr> fragmentsByBlock;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (fragment.getId() != index)
        rejectPlan("compute-fragment IDs do not match the node inventory");
      sim::SimFuncOp function =
          lookupFunction(fragment.getFunction().getValue());
      Block *block =
          function ? lookupComputeGraphBlock(function, fragment.getBlock())
                   : nullptr;
      if (!function || !block)
        rejectPlan("compute graph references a stale function or block");
      else {
        fragmentsByBlock.try_emplace(block, fragment);
        if (fragment.getTier() == sim::ComputeTierKind::Native)
          continue;
        result.reasons.emplace_back(
            "compute graph contains a bytecode-only fragment");
        if (!isConcurrentColdActor(function))
          onlyConcurrentColdBoundaries = false;
        auto &fragments = result.bytecodeFragments[function.getOperation()];
        if (!llvm::is_contained(fragments, block))
          fragments.push_back(block);
      }
      continue;
    }
    if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(attribute)) {
      if (commit.getId() != index)
        rejectPlan("NBA commit IDs do not match the node inventory");
      if (!commit.getFrontierSites().empty())
        result.reasons.emplace_back(
            "NBA site requires DynamicFrontier storage");
      if (!commit.getFrontierSites().empty())
        onlyConcurrentColdBoundaries = false;
      continue;
    }
    if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(attribute)) {
      if (commit.getId() != index)
        rejectPlan("event commit IDs do not match the node inventory");
      if (!commit.getSites().empty())
        result.reasons.emplace_back("deferred events require dynamic storage");
      if (!commit.getSites().empty() &&
          !coldDeferredCommits.lookup(commit.getId()))
        onlyConcurrentColdBoundaries = false;
      continue;
    }
    rejectPlan("compute graph contains an unknown node kind");
  }

  module.walk([&](sim::SimFuncOp function) {
    if (isRuntimeClockCoordinator(function)) {
      result.runtimeOwnedFanoutActors.insert(function.getOperation());
      if (function->hasAttr("obelisk_sim.negative_timing_adjusted"))
        result.negativeTimingFanoutActors.insert(function.getOperation());
    }
    if (!isNegativeTimingDelayMonitor(function))
      return;
    result.runtimeOwnedFanoutActors.insert(function.getOperation());
    result.negativeTimingFanoutActors.insert(function.getOperation());
    // IEEE 1800-2017 31.9.1 makes the implicit delayed terminal a transport
    // copy of the original terminal. Keep this exact generated monitor in the
    // generic scheduler so its source wait is a real runtime subscription and
    // every source occurrence can register the positive-delay commit before
    // generated run-until advances time.
    result.reasons.emplace_back(
        "negative timing delay monitor requires runtime ordering");
    bytecodeActors.insert(function.getOperation());
  });

  // Clause 31.9.1 accepts only whole direct terminals in this tranche, so a
  // resource+descriptor key is the complete overlap index. Deduplicating here
  // makes writer classification linear in graph effects rather than quadratic
  // in the number of timing checks.
  DenseSet<uint64_t> runtimeObservedStorage;
  DenseSet<uint64_t> runtimeObservedNets;
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    sim::SimFuncOp function =
        fragment ? lookupFunction(fragment.getFunction().getValue())
                 : sim::SimFuncOp{};
    // Only the exact negative component joins the delayed-source publication
    // cohort. Unrelated Clause 31.7 coordinators in the same design retain
    // their established AOT plan.
    if (!fragment || !function ||
        !result.negativeTimingFanoutActors.contains(function.getOperation()))
      continue;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() == sim::ComputeEffectKind::Watch &&
          effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
          (effect.getResource() == sim::ComputeResourceKind::Storage ||
           effect.getResource() == sim::ComputeResourceKind::Net) &&
          !effect.getDynamic() && !effect.getDeferred() &&
          effect.getWidth() != 0)
        (effect.getResource() == sim::ComputeResourceKind::Storage
             ? runtimeObservedStorage
             : runtimeObservedNets)
            .insert(effect.getDescriptor());
    }
  }
  auto overlapsRuntimeObservedSource = [&](sim::ComputeEffectAttr write) {
    if (write.getEffect() != sim::ComputeEffectKind::Write ||
        write.getTarget() != sim::ComputeTargetKind::Descriptor ||
        (write.getResource() != sim::ComputeResourceKind::Storage &&
         write.getResource() != sim::ComputeResourceKind::Net) ||
        write.getDynamic() || write.getDeferred() || write.getWidth() == 0)
      return false;
    const DenseSet<uint64_t> &descriptors =
        write.getResource() == sim::ComputeResourceKind::Storage
            ? runtimeObservedStorage
            : runtimeObservedNets;
    return descriptors.contains(write.getDescriptor());
  };
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    sim::SimFuncOp function =
        fragment ? lookupFunction(fragment.getFunction().getValue())
                 : sim::SimFuncOp{};
    if (!fragment || !function || isRuntimeClockCoordinator(function) ||
        isNegativeTimingDelayMonitor(function) ||
        function.getEntryKind() == sim::EntryKind::RootInitializer)
      continue;
    if (!llvm::any_of(fragment.getEffects(), [&](Attribute effect) {
          return overlapsRuntimeObservedSource(
              cast<sim::ComputeEffectAttr>(effect));
        }))
      continue;
    Block *block = lookupComputeGraphBlock(function, fragment.getBlock());
    if (!block)
      continue;
    // IEEE 1800-2017 Clause 31.7 primary occurrences and Clause 31.9.1
    // delayed-terminal source occurrences must reach their runtime-owned
    // observers in the publication cohort. Keep every non-root overlapping
    // writer activation behind a generic checkpoint; otherwise a generated
    // island could publish and advance time without waking the observer.
    if (!result.runtimeObservedWriterActors.insert(function.getOperation())
             .second)
      continue;
    for (Block &owned : function.getBody())
      requireBytecodeFragment(owned.getTerminator(),
                              "runtime-observed source publication",
                              /*certifiedCold=*/true);
  }

  auto isolatesConcurrentColdActors = [&](sim::ComputeGroupAttr group) {
    llvm::DenseSet<uint32_t> nonColdMembers;
    bool hasConcurrentColdActor = false;
    for (int64_t member : group.getFragments().asArrayRef()) {
      if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
        continue;
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
          nodes[static_cast<size_t>(member)]);
      sim::SimFuncOp function =
          fragment ? lookupFunction(fragment.getFunction().getValue())
                   : sim::SimFuncOp{};
      if (function && isConcurrentColdActor(function))
        hasConcurrentColdActor = true;
      else if (fragment)
        nonColdMembers.insert(static_cast<uint32_t>(member));
    }
    if (!hasConcurrentColdActor)
      return false;

    llvm::DenseMap<uint32_t, SmallVector<uint32_t>> successors;
    llvm::DenseMap<uint32_t, unsigned> indegree;
    for (Attribute edgeAttribute : graph.getEdges()) {
      auto edge = dyn_cast<sim::ComputeEdgeAttr>(edgeAttribute);
      if (!edge || edge.getKind() == sim::ComputeEdgeKind::Resume ||
          edge.getKind() == sim::ComputeEdgeKind::Spawn ||
          !nonColdMembers.contains(edge.getSource()) ||
          !nonColdMembers.contains(edge.getTarget()))
        continue;
      auto &targets = successors[edge.getSource()];
      if (llvm::is_contained(targets, edge.getTarget()))
        continue;
      targets.push_back(edge.getTarget());
    }

    llvm::DenseMap<uint32_t, unsigned> discovery;
    llvm::DenseMap<uint32_t, unsigned> lowlink;
    llvm::DenseSet<uint32_t> onStack;
    SmallVector<uint32_t> stack;
    SmallVector<SmallVector<uint32_t>> components;
    unsigned nextIndex = 0;
    std::function<void(uint32_t)> visit = [&](uint32_t member) {
      discovery[member] = nextIndex;
      lowlink[member] = nextIndex++;
      stack.push_back(member);
      onStack.insert(member);
      for (uint32_t successor : successors[member]) {
        if (!discovery.count(successor)) {
          visit(successor);
          lowlink[member] = std::min(lowlink[member], lowlink[successor]);
        } else if (onStack.contains(successor)) {
          lowlink[member] = std::min(lowlink[member], discovery[successor]);
        }
      }
      if (lowlink[member] != discovery[member])
        return;
      SmallVector<uint32_t> component;
      while (true) {
        uint32_t node = stack.pop_back_val();
        onStack.erase(node);
        component.push_back(node);
        if (node == member)
          break;
      }
      components.push_back(std::move(component));
    };
    for (uint32_t member : nonColdMembers)
      if (!discovery.count(member))
        visit(member);

    llvm::DenseMap<uint32_t, unsigned> componentOf;
    for (auto [index, component] : llvm::enumerate(components))
      for (uint32_t member : component)
        componentOf[member] = index;
    llvm::DenseMap<uint32_t, SmallVector<uint32_t>> processSuccessors;
    for (uint32_t member : nonColdMembers)
      indegree.try_emplace(member, 0);
    for (Attribute edgeAttribute : graph.getEdges()) {
      auto edge = dyn_cast<sim::ComputeEdgeAttr>(edgeAttribute);
      if (!edge || edge.getKind() != sim::ComputeEdgeKind::ProcessOrder ||
          !nonColdMembers.contains(edge.getSource()) ||
          !nonColdMembers.contains(edge.getTarget()) ||
          componentOf[edge.getSource()] != componentOf[edge.getTarget()])
        continue;
      auto &targets = processSuccessors[edge.getSource()];
      if (llvm::is_contained(targets, edge.getTarget()))
        continue;
      targets.push_back(edge.getTarget());
      ++indegree[edge.getTarget()];
    }
    SmallVector<uint32_t> ready;
    for (uint32_t member : nonColdMembers)
      if (indegree[member] == 0)
        ready.push_back(member);
    size_t visited = 0;
    while (!ready.empty()) {
      uint32_t member = ready.pop_back_val();
      ++visited;
      for (uint32_t successor : processSuccessors[member])
        if (--indegree[successor] == 0)
          ready.push_back(successor);
    }
    if (visited != nonColdMembers.size())
      return false;
    // IEEE 1800-2017 Clause 31 coordinators can close scheduling SCCs around
    // ordinary actors. Recompute the induced SCCs with every scheduling edge
    // (all except resume/spawn, matching ComputeGraph.cpp), then apply the same
    // ProcessOrder-cycle test used to distinguish a generic control loop from
    // native-ready-node convergence. Any remaining procedural cycle belongs
    // to the user design and cannot inherit coordinator-only hybrid admission.
    return true;
  };
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = dyn_cast<sim::ComputeRegionAttr>(regionAttribute);
    if (!region)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = dyn_cast<sim::ComputeGroupAttr>(groupAttribute);
      if (!group)
        continue;
      StringRef reason;
      bool actorLocalColdLoop = false;
      if (group.getSchedule() == sim::ComputeScheduleKind::ControlLoop) {
        reason = "control-loop group requires bytecode scheduling";
        actorLocalColdLoop = isolatesConcurrentColdActors(group);
      }
      // Native ready-node scheduling is itself a dirty-set fixpoint: a write
      // that wakes an earlier-ranked member restarts the scan at that member.
      // Convergence SCCs therefore need no bytecode handoff.  Control loops
      // remain generic because progress is not driven solely by state change.
      else if (group.getSchedule() == sim::ComputeScheduleKind::Convergence)
        continue;
      else if (group.getFragments().size() > 1)
        reason = "multi-member compute group requires bytecode scheduling";
      else
        continue;
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          continue;
        auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
            nodes[static_cast<size_t>(member)]);
        if (!fragment)
          continue;
        sim::SimFuncOp function =
            lookupFunction(fragment.getFunction().getValue());
        if (actorLocalColdLoop && !isConcurrentColdActor(function))
          continue;
        Block *block =
            function ? lookupComputeGraphBlock(function, fragment.getBlock())
                     : nullptr;
        if (block)
          requireBytecodeFragment(block->getTerminator(), reason);
      }
    }
  }

  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    rejectPlan("missing root initializer");
  DenseMap<StringRef, unsigned> rootSpawnCounts;
  DenseMap<StringRef, unsigned> totalSpawnCounts;
  if (root)
    root.walk(
        [&](sim::SimSpawnOp spawn) { ++rootSpawnCounts[spawn.getCallee()]; });
  module.walk(
      [&](sim::SimSpawnOp spawn) { ++totalSpawnCounts[spawn.getCallee()]; });
  SmallVector<sim::SimFuncOp> staticNestedActors;
  auto isStaticPersistentMonitorSpawn = [&](sim::SimSpawnOp spawn,
                                            sim::SimFuncOp owner,
                                            sim::SimFuncOp target) {
    if (!isPersistentMonitorActor(target) ||
        owner.getEntryKind() != sim::EntryKind::Initial ||
        rootSpawnCounts.lookup(owner.getSymName()) != 1 ||
        totalSpawnCounts.lookup(target.getSymName()) != 1 ||
        spawn->getBlock() != &owner.getBody().front() ||
        !spawn.getResult().hasOneUse())
      return false;
    auto registration =
        dyn_cast<sim::SimMonitorRegisterOp>(*spawn.getResult().user_begin());
    return registration && registration.getProcess() == spawn.getResult();
  };
  module.walk([&](sim::SimSpawnOp spawn) {
    sim::SimFuncOp owner = spawn->getParentOfType<sim::SimFuncOp>();
    if (!owner || owner != root) {
      sim::SimFuncOp target = lookupFunction(spawn.getCallee());
      if (owner && target &&
          isStaticPersistentMonitorSpawn(spawn, owner, target)) {
        if (!llvm::is_contained(staticNestedActors, target))
          staticNestedActors.push_back(target);
        return;
      }
      bool concurrentCold = target && isConcurrentColdActor(target);
      if (isNegativeTimingDelayMonitorSpawn(spawn, target)) {
        // IEEE 1800-2017 31.9.1 transport commits must execute their first
        // calendar suspension before generated run-until may advance time.
        // Keep the descriptor-bound monitor actor native, but hand this exact
        // source-activation block to bytecode so the generic scheduler
        // registers the positive-delay deadline before re-entering AOT.
        requireBytecodeFragment(spawn, "negative timing transport activation",
                                /*certifiedCold=*/true);
      } else if (concurrentCold && !isNegativeTimingDelayCommit(target)) {
        result.reasons.emplace_back("dynamic spawn multiplicity");
      } else {
        requireBytecodeFragment(spawn, "dynamic spawn multiplicity");
      }
      if (target)
        dynamicActors.insert(target.getOperation());
      return;
    }
    if (rootSpawnCounts.lookup(spawn.getCallee()) != 1) {
      result.reasons.emplace_back("duplicate statically spawned process");
      onlyConcurrentColdBoundaries = false;
      if (sim::SimFuncOp target = lookupFunction(spawn.getCallee()))
        dynamicActors.insert(target.getOperation());
    }
  });

  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Task ||
        (function.getEntryKind() == sim::EntryKind::Fork &&
         !llvm::is_contained(staticNestedActors, function))) {
      result.reasons.emplace_back("task, await, or join control is present");
      onlyConcurrentColdBoundaries &= isConcurrentColdActor(function);
      dynamicActors.insert(function.getOperation());
    }
    // Statically bound actors retain their semantic home region in the
    // scheduler record.  Native ready nodes are ranked from the region-ordered
    // compute graph, while hybrid arbitration compares queuedRegion before
    // rank and insertion sequence.  Observed, Reactive, and Postponed actors
    // therefore need no generic-only exclusion merely because they are not
    // Active.
  });
  module.walk([&](Operation *operation) {
    bool hasRealValue =
        llvm::any_of(operation->getOperandTypes(),
                     [](Type type) { return isa<FloatType>(type); }) ||
        llvm::any_of(operation->getResultTypes(),
                     [](Type type) { return isa<FloatType>(type); });
    if (isa<sim::SimStopOp, sim::SimFatalOp>(operation)) {
      // Termination is scheduler-global control, not a local bytecode
      // boundary. A hybrid plan can execute an excluded actor through the
      // generic scheduler, but termination can return from the coordinator
      // before the generic region barrier drains updates queued by that
      // actor. Keep the complete design under the generic scheduler so
      // termination observes the same ordered region state as the process
      // that requested it.
      rejectPlan("fatal or stop control requires generic ordering");
    } else if (isa<sim::SimProcessControlOp, sim::SimProgramExitOp>(
                   operation)) {
      // A process object can dynamically name any native or bytecode actor,
      // including an ancestor of the current activation. Keep the complete
      // scheduler under runtime ownership until generated AOT plans have a
      // transactional actor suspend/resume/kill protocol.
      rejectPlan("process control requires generic ordering");
    } else if (isa<sim::SimDPICallOp>(operation)) {
      requireBytecodeFragment(operation, "DPI reentrancy is present");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimDynamicOverrideOp>(operation)) {
      // Dynamic override ownership can retire a detached evaluator process.
      // Keep the scheduler fully runtime-owned until generated plans expose a
      // transactional actor-removal protocol.
      rejectPlan("dynamic override ownership requires generic ordering");
    } else if (isa<sim::SimRefStoreInertialPathOp>(operation)) {
      // This operation may either publish storage immediately or insert a
      // keyed calendar transaction after per-bit path arbitration. The AOT
      // compute graph has no node/effect encoding for that dynamic choice, so
      // a generated ready-node closure could miss the storage notification or
      // fail to hand the timed transaction back to the runtime scheduler.
      // Keep only the containing procedural actor runtime-owned; unrelated
      // actors remain eligible for the native AOT plan.
      requireBytecodeFragment(operation,
                              "procedural path scheduling is runtime-owned");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimDriverDriveInertialOp,
                   sim::SimDriverDriveInertialPathOp,
                   sim::SimDriverDriveInertialStrengthPairOp,
                   sim::SimDriverDriveInertialPathStrengthPairOp>(operation) &&
               operation->getParentOfType<sim::SimFuncOp>().getEntryKind() ==
                   sim::EntryKind::Continuous) {
      // An explicit delayed continuous assignment evaluates once at time
      // zero, before it becomes event-driven. The generated ready-node plan
      // currently models only later sensitivity activations and can therefore
      // omit that initial inertial publication when a source is initialized
      // procedurally in the same slot. Keep this uncommon calendar-event shape
      // in the compact generic scheduler until the AOT graph has an explicit
      // bootstrap edge for the post-evaluation wait continuation.
      rejectPlan("delayed continuous assignment requires generic ordering");
    } else if (isa<sim::SimPassSwitchControlDelayedOp,
                   sim::SimMosDriveDelayedOp>(operation)) {
      // A delayed control publication changes frozen connectivity from a
      // runtime calendar event. The generic scheduler owns that topology
      // barrier and its ordered net notifications; generated AOT plans do not
      // yet contain an equivalent topology-event commit node.
      rejectPlan("delayed switch contribution requires generic ordering");
    } else if (isa<sim::SimOverrideOp, sim::SimReleaseOverrideOp>(operation)) {
      requireBytecodeFragment(operation, "force/release state is present");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimManagedNBAEnqueueOp,
                   sim::SimReferencePathNBAEnqueueOp>(operation)) {
      requireBytecodeFragment(operation,
                              "managed or automatic NBA destination");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimSuspendClockSetOp>(operation)) {
      // Exact occurrence cohorts retain publication-wave state in the generic
      // scheduler. Keep only the feature coordinator in hybrid bytecode; all
      // ordinary assertion and procedural actors remain statically eligible.
      if (!isRuntimeClockCoordinator(findContainingFunction(operation))) {
        rejectPlan("clock cohort wait lacks exact coordinator provenance");
      } else {
        requireBytecodeFragment(operation,
                                "clock cohort wait requires runtime ordering");
        excludeBytecodeActor(operation);
      }
    } else if (isa<sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp,
                   sim::SimSuspendObserveOp>(operation)) {
      requireBytecodeFragment(operation, "computed or conditional wait");
      excludeBytecodeActor(operation);
    } else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(operation)) {
      // An explicit sensitivity list is a fixed direct wait when graph
      // provenance resolved every watched range to a storage/net descriptor.
      // Such waits use the same stable continuation and fanout records as the
      // one-handle change/edge forms.
      auto fragment = fragmentsByBlock.find(operation->getBlock());
      bool fixed = any.getSiteAttr() && any.getSiteAttr().getId() != 0 &&
                   any.getWatched().size() != 0 &&
                   fragment != fragmentsByBlock.end();
      unsigned watchCount = 0;
      if (fixed)
        for (Attribute effectAttribute : fragment->second.getEffects()) {
          auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
          if (effect.getEffect() != sim::ComputeEffectKind::Watch)
            continue;
          ++watchCount;
          fixed &= effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
                   !effect.getDynamic() && !effect.getDeferred() &&
                   effect.getWidth() != 0 &&
                   (effect.getResource() == sim::ComputeResourceKind::Storage ||
                    effect.getResource() == sim::ComputeResourceKind::Net) &&
                   effect.getTrigger() != sim::ComputeTriggerKind::None &&
                   effect.getTrigger() != sim::ComputeTriggerKind::Event;
        }
      fixed &= watchCount != 0;
      if (!fixed) {
        requireBytecodeFragment(operation, "computed or conditional wait");
        excludeBytecodeActor(operation);
      }
    } else if (isa<sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp>(
                   operation)) {
      requireBytecodeFragment(operation, "event wait requires dynamic state");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp>(
                   operation)) {
      requireBytecodeFragment(operation,
                              "managed wait requires dynamic runtime state");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
                   sim::SimSuspendChildrenOp, sim::SimTaskCallOp,
                   sim::SimClassVirtualTaskCallOp>(operation)) {
      requireBytecodeFragment(operation,
                              "task, await, or join control is present");
      excludeBytecodeActor(operation);
    } else if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation)) {
      auto timing = delay.getTimingAttr();
      if (!timing || timing.getKind() != sim::ComputeTimingKind::Calendar) {
        result.reasons.emplace_back("dynamic deadline");
        onlyConcurrentColdBoundaries = false;
      }
    } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
      auto site = nba->getAttrOfType<sim::NBASiteAttr>("site");
      if (!site)
        requireBytecodeFragment(operation, "NBA site metadata is missing");
      else if (site.getTiming())
        requireBytecodeFragment(operation, "delayed NBA site");
      else if (site.getStorage() == sim::ComputeNBAStorageKind::DynamicFrontier)
        requireBytecodeFragment(operation,
                                "NBA site requires DynamicFrontier storage");
      if (!site || site.getTiming() ||
          site.getStorage() == sim::ComputeNBAStorageKind::DynamicFrontier)
        excludeBytecodeActor(operation);
    } else if (isa<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp>(operation)) {
      if (!operation->getAttrOfType<sim::ContinuationSiteAttr>("site"))
        requireBytecodeFragment(operation,
                                "continuation-site metadata is missing");
    }
    // Real-valued publications use IEEE comparison semantics, including a
    // NaN self-assignment being observable. The generated direct-wait path
    // currently models only packed value/unknown planes, so it cannot own a
    // real-reactive continuation without losing the runtime publication. Keep
    // the complete containing actor in Tier 3 until real transition records
    // are part of the native schedule ABI.
    if (hasRealValue) {
      requireBytecodeFragment(operation,
                              "real-valued reactive state requires bytecode");
      excludeBytecodeActor(operation);
    }
    if (llvm::any_of(operation->getOperandTypes(), isManagedType) ||
        llvm::any_of(operation->getResultTypes(), isManagedType)) {
      requireBytecodeFragment(operation, "managed or string state is present");
      excludeBytecodeActor(operation);
    }
  });

  llvm::sort(result.reasons);
  result.reasons.erase(
      std::unique(result.reasons.begin(), result.reasons.end()),
      result.reasons.end());
  if (invalidPlan || !root)
    return result;

  uint32_t slot = 0;
  if (!dynamicActors.contains(root.getOperation()) &&
      !bytecodeActors.contains(root.getOperation()))
    result.actorSlots[root.getOperation()] = slot++;
  root.walk([&](sim::SimSpawnOp spawn) {
    sim::SimFuncOp target = lookupFunction(spawn.getCallee());
    if (!target || dynamicActors.contains(target.getOperation()) ||
        bytecodeActors.contains(target.getOperation()))
      return;
    if (result.actorSlots.try_emplace(target.getOperation(), slot).second)
      ++slot;
  });
  for (sim::SimFuncOp function : staticNestedActors) {
    if (dynamicActors.contains(function.getOperation()) ||
        bytecodeActors.contains(function.getOperation()))
      continue;
    if (result.actorSlots.try_emplace(function.getOperation(), slot).second)
      ++slot;
  }
  result.eligible = !result.actorSlots.empty();
  result.fullyEligible = result.eligible && result.reasons.empty();
  // Forced hybrid admission is actor/block-provenance based. Every bytecode
  // boundary must have been classified at the operation that created it as an
  // exact coordinator or one of the preexisting internal assertion callback
  // shapes. No global reason-string allowlist can make an unrelated user
  // control loop eligible merely because a coordinator is also present.
  result.forcedHybridEligible =
      result.eligible && !result.fullyEligible && onlyConcurrentColdBoundaries;
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    uint64_t weight = std::max<uint64_t>(fragment.getCost(), 1);
    result.totalGraphCost += weight;
    sim::SimFuncOp function = lookupFunction(fragment.getFunction().getValue());
    Block *block = function
                       ? lookupComputeGraphBlock(function, fragment.getBlock())
                       : nullptr;
    if (!function || !block ||
        !result.actorSlots.contains(function.getOperation()))
      continue;
    auto bytecode = result.bytecodeFragments.find(function.getOperation());
    if (bytecode != result.bytecodeFragments.end() &&
        llvm::is_contained(bytecode->second, block))
      continue;
    result.nativeGraphCost += weight;
  }
  // The legacy partial AOT wrapper ultimately runs the generic scheduler, so
  // selecting it automatically only adds planning and code-generation work.
  // A fully closed schedule executes generated nodes and remains profitable.
  // Explicit AOT and Eval retain their separate strict admission contracts.
  result.aotCostEffective = result.fullyEligible;
  return result;
}

} // namespace obelisk::analysis
