//===- PlanStaticSuperstep.cpp - Plan guarded native supersteps ----------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"

#include "llvm/ADT/DenseSet.h"

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

  for (Attribute attribute : graph.getNodes()) {
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (fragment.getTier() != sim::ComputeTierKind::Native) {
        reject("bytecode-only compute fragment");
        continue;
      }
      for (Attribute effectAttribute : fragment.getEffects()) {
        auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
        if (effect.getEffect() != sim::ComputeEffectKind::Watch)
          continue;
        bool exact =
            effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
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
      if (group.getSchedule() == sim::ComputeScheduleKind::ControlLoop)
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
        reject("spawn outside the root initializer");
        return;
      }
      appendActor(design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee()));
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
