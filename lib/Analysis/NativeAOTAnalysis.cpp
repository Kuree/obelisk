//===- NativeAOTAnalysis.cpp - Native scheduler eligibility --------------===//

#include "obelisk/Analysis/NativeAOTAnalysis.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

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
      else if (fragment.getTier() != sim::ComputeTierKind::Native) {
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
      else if (group.getSchedule() == sim::ComputeScheduleKind::Convergence)
        reason = "convergence group requires bytecode scheduling";
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
                   sim::SimSuspendAnyOp, sim::SimSuspendObserveOp>(operation)) {
      requireBytecodeFragment(operation, "computed or conditional wait");
      excludeBytecodeActor(operation);
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
