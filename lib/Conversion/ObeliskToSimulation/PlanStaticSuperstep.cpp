//===- PlanStaticSuperstep.cpp - Plan guarded native supersteps ----------===//

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <functional>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPLANSTATICSUPERSTEPPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimPlanStaticSuperstepPass
    : public impl::ObeliskSimPlanStaticSuperstepPassBase<
          ObeliskSimPlanStaticSuperstepPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

void ObeliskSimPlanStaticSuperstepPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError("static superstep planning requires a verified "
                       "compute graph");
    return signalPassFailure();
  }
  design->removeAttr(sim::metadata::staticSuperstep);

  std::string reason;
  auto reject = [&](StringRef message) {
    if (reason.empty())
      reason = message.str();
  };
  if (graph.getVersion() != sim::metadata::schemaVersion)
    reject("unsupported compute-graph version");
  if (graph.getWorkers() != 1)
    reject("static supersteps require one worker");

  auto isRuntimeOwnedColdActor = [&](sim::SimFuncOp function) {
    return analysis::isRuntimeClockCoordinator(function) ||
           analysis::isNegativeTimingDelayMonitor(function) ||
           analysis::isCovergroupClockingSamplerActor(function);
  };
  auto isolatesRuntimeOwnedColdActors = [&](sim::ComputeGroupAttr group) {
    llvm::DenseSet<uint32_t> nativeMembers;
    bool hasRuntimeOwnedActor = false;
    for (int64_t member : group.getFragments().asArrayRef()) {
      if (member < 0 ||
          static_cast<uint64_t>(member) >= graph.getNodes().size())
        continue;
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
          graph.getNodes()[static_cast<size_t>(member)]);
      sim::SimFuncOp function = fragment
                                    ? design.lookupSymbol<sim::SimFuncOp>(
                                          fragment.getFunction().getValue())
                                    : nullptr;
      if (isRuntimeOwnedColdActor(function))
        hasRuntimeOwnedActor = true;
      else if (fragment)
        nativeMembers.insert(static_cast<uint32_t>(member));
    }
    if (!hasRuntimeOwnedActor)
      return false;

    // Recompute scheduling SCCs after removing the runtime-owned actors. This
    // matches the graph's definition of a scheduling edge: resume leaves the
    // region and spawn starts another actor, so neither closes an SCC here.
    llvm::DenseMap<uint32_t, SmallVector<uint32_t>> successors;
    for (Attribute edgeAttribute : graph.getEdges()) {
      auto edge = dyn_cast<sim::ComputeEdgeAttr>(edgeAttribute);
      if (!edge || edge.getKind() == sim::ComputeEdgeKind::Resume ||
          edge.getKind() == sim::ComputeEdgeKind::Spawn ||
          !nativeMembers.contains(edge.getSource()) ||
          !nativeMembers.contains(edge.getTarget()))
        continue;
      auto &targets = successors[edge.getSource()];
      if (!llvm::is_contained(targets, edge.getTarget()))
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
    for (uint32_t member : nativeMembers)
      if (!discovery.count(member))
        visit(member);

    llvm::DenseMap<uint32_t, unsigned> componentOf;
    for (auto [index, component] : llvm::enumerate(components))
      for (uint32_t member : component)
        componentOf[member] = index;

    // A residual ProcessOrder cycle is procedural control, not a native
    // ready-node convergence loop. It cannot inherit the coordinator's cold
    // runtime boundary.
    llvm::DenseMap<uint32_t, SmallVector<uint32_t>> processSuccessors;
    llvm::DenseMap<uint32_t, unsigned> indegree;
    for (uint32_t member : nativeMembers)
      indegree.try_emplace(member, 0);
    for (Attribute edgeAttribute : graph.getEdges()) {
      auto edge = dyn_cast<sim::ComputeEdgeAttr>(edgeAttribute);
      if (!edge || edge.getKind() != sim::ComputeEdgeKind::ProcessOrder ||
          !nativeMembers.contains(edge.getSource()) ||
          !nativeMembers.contains(edge.getTarget()) ||
          componentOf[edge.getSource()] != componentOf[edge.getTarget()])
        continue;
      auto &targets = processSuccessors[edge.getSource()];
      if (llvm::is_contained(targets, edge.getTarget()))
        continue;
      targets.push_back(edge.getTarget());
      ++indegree[edge.getTarget()];
    }
    SmallVector<uint32_t> ready;
    for (uint32_t member : nativeMembers)
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
    return visited == nativeMembers.size();
  };

  for (Attribute attribute : graph.getNodes()) {
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (fragment.getTier() != sim::ComputeTierKind::Native) {
        reject("bytecode-only compute fragment");
        continue;
      }
      sim::SimFuncOp function = design.lookupSymbol<sim::SimFuncOp>(
          fragment.getFunction().getValue());
      bool runtimeOwnedColdActor = isRuntimeOwnedColdActor(function);
      for (Attribute effectAttribute : fragment.getEffects()) {
        auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
        if (effect.getEffect() != sim::ComputeEffectKind::Watch)
          continue;
        // Computed covergroup events are indexed by the runtime from their
        // serialized observer dependencies. Their compiler-owned registration
        // actor is excluded from the static superstep, and static publication
        // still enters the central transition recorder before fanout. Its
        // deliberately abstract graph watch therefore does not make the
        // remaining generated island dynamic.
        if (runtimeOwnedColdActor)
          continue;
        bool exact = effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
                     !effect.getDynamic() && !effect.getDeferred() &&
                     effect.getWidth() != 0 &&
                     effect.getTrigger() != sim::ComputeTriggerKind::None;
        if (!exact)
          reject("dynamic or unsupported sensitivity");
      }
      continue;
    }
    if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(attribute)) {
      if (!commit.getFrontierSites().empty())
        reject("dynamic NBA frontier");
      continue;
    }
    if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(attribute)) {
      if (!commit.getSites().empty())
        reject("deferred event frontier");
      continue;
    }
    reject("unknown compute node");
  }
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      // A convergence group is still a closed, statically indexed native
      // schedule. The AOT ready-node bitset performs its fixpoint iteration;
      // neither multiple members nor a back edge requires bytecode or an
      // externally mutable event queue. Only a control-loop group has a
      // scheduler-dependent boundary that the clean transaction cannot
      // certify.
      // IEEE 1800-2017 16.14 and Clause 31 coordinator/transport-monitor
      // loops retain their exact cohort ordering in the runtime. They are a
      // cold hybrid island, not a reason to discard an otherwise closed
      // native superstep.
      if (group.getSchedule() == sim::ComputeScheduleKind::ControlLoop &&
          !isolatesRuntimeOwnedColdActors(group))
        reject("control-loop compute group");
    }
  }

  sim::SimFuncOp root;
  design.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    reject("missing root initializer");

  SmallVector<Attribute> actors;
  llvm::SmallDenseSet<StringRef, 16> actorNames;
  auto appendActor = [&](sim::SimFuncOp function) {
    if (!function || !actorNames.insert(function.getSymName()).second) {
      reject("duplicate or unresolved static actor");
      return;
    }
    actors.push_back(
        FlatSymbolRefAttr::get(design.getContext(), function.getSymName()));
  };
  if (root) {
    appendActor(root);
    design.walk([&](sim::SimSpawnOp spawn) {
      if (spawn->getParentOfType<sim::SimFuncOp>() != root) {
        sim::SimFuncOp actor =
            design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee());
        // IEEE 1800-2017 31.9.1 requires a transport-delayed copy of each
        // affected terminal.  Its compiler-generated one-shot commit is the
        // only non-root spawn admitted here: the shared structural certificate
        // also used by native AOT proves that the helper cannot introduce an
        // unplanned watcher or recursively spawn work.
        if (analysis::isNegativeTimingDelayMonitorSpawn(spawn, actor) ||
            isRuntimeOwnedColdActor(actor))
          return;
        reject("spawn outside the root initializer");
        return;
      }
      sim::SimFuncOp actor =
          design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee());
      // Clause 31.7 coordinators and the exact Clause 31.9.1 transport
      // monitors keep their waits in the generic scheduler. The latter is
      // certified by its complete CFG and unique commit-spawn shape.
      if (isRuntimeOwnedColdActor(actor))
        return;
      appendActor(actor);
    });
  }

  if (!reason.empty()) {
    if (missedRemarks)
      design.emitRemark() << "static superstep not planned: " << reason;
    return;
  }
  design->setAttr(sim::metadata::staticSuperstep,
                  sim::StaticSuperstepAttr::get(
                      design.getContext(), sim::metadata::schemaVersion, graph,
                      ArrayAttr::get(design.getContext(), actors)));
}

} // namespace
} // namespace obelisk
