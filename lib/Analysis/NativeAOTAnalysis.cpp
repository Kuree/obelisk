//===- NativeAOTAnalysis.cpp - Native scheduler eligibility --------------===//

#include "obelisk/Analysis/NativeAOTAnalysis.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <functional>

using namespace mlir;

namespace obelisk::analysis {
namespace {

bool isManagedType(Type type) {
  if (isa<sim::StringType, sim::ClassHandleType, sim::DynamicArrayType,
          sim::QueueType, sim::AssocArrayType, sim::ReferencePathType,
          sim::ManagedRefType, sim::ArgumentRefType>(type))
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

} // namespace

NativeAOTAnalysis NativeAOTAnalysis::compute(ModuleOp module) {
  NativeAOTAnalysis result;
  bool invalidPlan = false;
  llvm::SmallDenseSet<Operation *> dynamicActors;
  llvm::SmallDenseSet<Operation *> bytecodeActors;
  auto rejectPlan = [&](StringRef reason) {
    invalidPlan = true;
    result.reasons.emplace_back(reason);
  };
  auto requireBytecodeFragment = [&](Operation *operation, StringRef reason) {
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
      Value other = xorOp.getLhs() == load.getResult() ? xorOp.getRhs()
                    : xorOp.getRhs() == load.getResult() ? xorOp.getLhs()
                                                         : Value{};
      auto one = other ? other.getDefiningOp<arith::ConstantOp>() : nullptr;
      auto integer = one ? dyn_cast<IntegerAttr>(one.getValue()) : IntegerAttr{};
      if (!integer || integer.getValue().getBitWidth() != 1 ||
          !integer.getValue().isOne())
        return;
    }
    Block *wait = delay->getBlock();
    Block *body = delay.getContinuation();
    auto back = dyn_cast<cf::BranchOp>(body->getTerminator());
    result.periodicClockCandidate =
        back && back.getDest() == wait && wait->getNumSuccessors() == 1 &&
        wait->getSuccessor(0) == body;
  });

  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  if (!design || !design.getComputeGraphAttr()) {
    rejectPlan("missing compute-graph metadata");
    return result;
  }
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (graph.getVersion() != sim::metadata::schemaVersion)
    rejectPlan("unsupported compute-graph version");
  if (graph.getWorkers() != 1)
    rejectPlan("AOT scheduling requires one worker");
  ArrayAttr nodes = graph.getNodes();
  DenseMap<Block *, sim::ComputeFragmentAttr> fragmentsByBlock;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (fragment.getId() != index)
        rejectPlan("compute-fragment IDs do not match the node inventory");
      sim::SimFuncOp function = design.lookupSymbol<sim::SimFuncOp>(
          fragment.getFunction().getValue());
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
      continue;
    }
    if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(attribute)) {
      if (commit.getId() != index)
        rejectPlan("event commit IDs do not match the node inventory");
      if (!commit.getSites().empty())
        result.reasons.emplace_back("deferred events require dynamic storage");
      continue;
    }
    rejectPlan("compute graph contains an unknown node kind");
  }
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = dyn_cast<sim::ComputeRegionAttr>(regionAttribute);
    if (!region)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = dyn_cast<sim::ComputeGroupAttr>(groupAttribute);
      if (!group)
        continue;
      StringRef reason;
      if (group.getSchedule() == sim::ComputeScheduleKind::ControlLoop)
        reason = "control-loop group requires bytecode scheduling";
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
        sim::SimFuncOp function = design.lookupSymbol<sim::SimFuncOp>(
            fragment.getFunction().getValue());
        Block *block =
            function ? lookupComputeGraphBlock(function, fragment.getBlock())
                     : nullptr;
        if (block)
          requireBytecodeFragment(block->getTerminator(), reason);
      }
    }
  }

  DenseMap<StringRef, unsigned> rootSpawnCounts;
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    rejectPlan("missing root initializer");
  module.walk([&](sim::SimSpawnOp spawn) {
    sim::SimFuncOp owner = spawn->getParentOfType<sim::SimFuncOp>();
    if (!owner || owner != root) {
      requireBytecodeFragment(spawn, "dynamic spawn multiplicity");
      if (sim::SimFuncOp target =
              design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee()))
        dynamicActors.insert(target.getOperation());
      return;
    }
    if (++rootSpawnCounts[spawn.getCallee()] != 1) {
      result.reasons.emplace_back("duplicate statically spawned process");
      if (sim::SimFuncOp target =
              design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee()))
        dynamicActors.insert(target.getOperation());
    }
  });

  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Task ||
        function.getEntryKind() == sim::EntryKind::Fork) {
      result.reasons.emplace_back("task, await, or join control is present");
      dynamicActors.insert(function.getOperation());
    }
    if (function.getHomeRegion() != sim::EventRegion::Active) {
      result.reasons.emplace_back(
          "non-Active process scheduling requires generic ordering");
      bytecodeActors.insert(function.getOperation());
    }
  });
  module.walk([&](Operation *operation) {
    if (isa<sim::SimStopOp, sim::SimFatalOp>(operation)) {
      result.reasons.emplace_back(
          "fatal or stop control requires generic ordering");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimDPICallOp>(operation)) {
      requireBytecodeFragment(operation, "DPI reentrancy is present");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimOverrideOp, sim::SimReleaseOverrideOp>(operation)) {
      requireBytecodeFragment(operation, "force/release state is present");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimManagedNBAEnqueueOp,
                   sim::SimReferencePathNBAEnqueueOp>(operation)) {
      requireBytecodeFragment(operation,
                              "managed or automatic NBA destination");
      excludeBytecodeActor(operation);
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
                   (effect.getResource() ==
                        sim::ComputeResourceKind::Storage ||
                    effect.getResource() == sim::ComputeResourceKind::Net) &&
                   effect.getTrigger() != sim::ComputeTriggerKind::None &&
                   effect.getTrigger() != sim::ComputeTriggerKind::Event;
        }
      fixed &= watchCount != 0;
      if (!fixed) {
        requireBytecodeFragment(operation, "computed or conditional wait");
        excludeBytecodeActor(operation);
      }
    } else if (isa<sim::SimSuspendEventOp>(operation)) {
      requireBytecodeFragment(operation, "event wait requires dynamic state");
      excludeBytecodeActor(operation);
    } else if (isa<sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
                   sim::SimSuspendChildrenOp, sim::SimTaskCallOp>(operation)) {
      requireBytecodeFragment(operation,
                              "task, await, or join control is present");
      excludeBytecodeActor(operation);
    } else if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation)) {
      auto timing = delay.getTimingAttr();
      if (!timing || timing.getKind() != sim::ComputeTimingKind::Calendar)
        result.reasons.emplace_back("dynamic deadline");
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
    sim::SimFuncOp target =
        design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee());
    if (!target || dynamicActors.contains(target.getOperation()) ||
        bytecodeActors.contains(target.getOperation()))
      return;
    if (result.actorSlots.try_emplace(target.getOperation(), slot).second)
      ++slot;
  });
  result.eligible = !result.actorSlots.empty();
  result.fullyEligible = result.eligible && result.reasons.empty();
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    uint64_t weight = std::max<uint64_t>(fragment.getCost(), 1);
    result.totalGraphCost += weight;
    sim::SimFuncOp function = design.lookupSymbol<sim::SimFuncOp>(
        fragment.getFunction().getValue());
    Block *block =
        function ? lookupComputeGraphBlock(function, fragment.getBlock())
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
  // Hybrid handoff has a substantial fixed cost. Select it automatically only
  // when the static actors cover at least seven tenths of estimated graph work;
  // a fully closed schedule remains unconditionally profitable. Explicit AOT
  // retains its strict full-eligibility contract.
  result.aotCostEffective =
      result.fullyEligible ||
      (result.totalGraphCost != 0 &&
       static_cast<long double>(result.nativeGraphCost) /
               static_cast<long double>(result.totalGraphCost) >=
           0.7L);
  return result;
}

} // namespace obelisk::analysis
