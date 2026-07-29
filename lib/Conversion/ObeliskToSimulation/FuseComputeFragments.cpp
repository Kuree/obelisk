//===- FuseComputeFragments.cpp - Plan static AOT execution batches ------===//

#include "ComputeFusion.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFUSECOMPUTEFRAGMENTSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimFuseComputeFragmentsPass final
    : public impl::ObeliskSimFuseComputeFragmentsPassBase<
          ObeliskSimFuseComputeFragmentsPass> {
public:
  using Base = impl::ObeliskSimFuseComputeFragmentsPassBase<
      ObeliskSimFuseComputeFragmentsPass>;
  using Base::Base;

  void runOnOperation() override;

private:
  Statistic plannedFusions{this, "planned-fusions",
                           "globally adjacent process-body fusions planned"};
  Statistic rejectedActors{
      this, "rejected-actors",
      "native direct-wait actors rejected by body eligibility"};
};

struct FusionCandidate {
  int64_t fragment;
  int64_t resumeTarget;
  uint32_t order;
  uint32_t entryOrder;
  Operation *function;
};

void ObeliskSimFuseComputeFragmentsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  StringRef metadataName = bodyFusion ? sim::metadata::staticBodyFusion
                                      : sim::metadata::staticFusion;
  design->removeAttr(metadataName);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph || graph.getWorkers() != 1 ||
      graph.getVpi() != sim::ComputeVPIMode::Off)
    return;

  ArrayAttr nodes = graph.getNodes();
  llvm::SmallDenseSet<int64_t> acyclicActive;
  DenseMap<int64_t, uint32_t> scheduleOrder;
  DenseMap<int64_t, int64_t> resumeTargets;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      for (int64_t member : group.getFragments().asArrayRef()) {
        scheduleOrder.try_emplace(member, nextOrder++);
        if (group.getSchedule() == sim::ComputeScheduleKind::Acyclic)
          acyclicActive.insert(member);
      }
    }
  }
  for (Attribute edgeAttribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(edgeAttribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Resume)
      resumeTargets.try_emplace(edge.getSource(), edge.getTarget());
  }
  SmallVector<int64_t> entryTargets;
  for (Attribute edgeAttribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(edgeAttribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Spawn &&
        scheduleOrder.contains(edge.getTarget()))
      entryTargets.push_back(edge.getTarget());
  }
  llvm::sort(entryTargets, [&](int64_t lhs, int64_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  entryTargets.erase(std::unique(entryTargets.begin(), entryTargets.end()),
                     entryTargets.end());
  DenseMap<Operation *, uint32_t> entryOrder;
  for (auto [order, target] : llvm::enumerate(entryTargets)) {
    auto fragment = target >= 0 && static_cast<uint64_t>(target) < nodes.size()
                        ? dyn_cast<sim::ComputeFragmentAttr>(
                              nodes[static_cast<size_t>(target)])
                        : sim::ComputeFragmentAttr{};
    sim::SimFuncOp function = fragment ? design.lookupSymbol<sim::SimFuncOp>(
                                             fragment.getFunction().getValue())
                                       : sim::SimFuncOp{};
    if (function)
      entryOrder.try_emplace(function.getOperation(),
                             static_cast<uint32_t>(order));
  }

  llvm::MapVector<Attribute, SmallVector<FusionCandidate>> bySensitivity;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment || fragment.getId() != index ||
        !acyclicActive.contains(static_cast<int64_t>(index)) ||
        fragment.getTier() != sim::ComputeTierKind::Native ||
        fragment.getRegion() != sim::ComputeRegionKind::Active ||
        (fragment.getAction() != sim::ComputeActionKind::SuspendChange &&
         fragment.getAction() != sim::ComputeActionKind::SuspendEdge))
      continue;

    sim::ComputeEffectAttr sensitivity;
    bool unsupported = false;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() != sim::ComputeEffectKind::Watch)
        continue;
      if (sensitivity) {
        unsupported = true;
        break;
      }
      sensitivity = effect;
    }
    if (unsupported || !sensitivity ||
        sensitivity.getTarget() != sim::ComputeTargetKind::Descriptor ||
        (sensitivity.getResource() != sim::ComputeResourceKind::Storage &&
         sensitivity.getResource() != sim::ComputeResourceKind::Net) ||
        sensitivity.getDynamic() || sensitivity.getDeferred() ||
        sensitivity.getWidth() == 0)
      continue;

    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    if (!function || (bodyFusion && !isComputeBodyFusionEligible(function))) {
      if (function)
        ++rejectedActors;
      continue;
    }
    auto resume = resumeTargets.find(static_cast<int64_t>(index));
    if (resume == resumeTargets.end())
      continue;
    auto functionEntry = entryOrder.find(function.getOperation());
    if (functionEntry == entryOrder.end())
      continue;

    bySensitivity[sensitivity].push_back(
        {static_cast<int64_t>(index), resume->second, 0, functionEntry->second,
         function.getOperation()});
  }

  SmallVector<Attribute> fusions;
  uint32_t id = 0;
  for (auto &[sensitivity, candidates] : bySensitivity) {
    if (candidates.size() < 2)
      continue;
    SmallVector<uint32_t> readyTargets = getComputeFusionReadyTargets(
        graph, cast<sim::ComputeEffectAttr>(sensitivity));
    llvm::erase_if(readyTargets, [&](uint32_t target) {
      return !scheduleOrder.contains(target);
    });
    llvm::sort(readyTargets, [&](uint32_t lhs, uint32_t rhs) {
      return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
    });
    readyTargets.erase(std::unique(readyTargets.begin(), readyTargets.end()),
                       readyTargets.end());
    DenseMap<int64_t, uint32_t> readyOrder;
    for (auto [order, target] : llvm::enumerate(readyTargets))
      readyOrder.try_emplace(target, static_cast<uint32_t>(order));
    llvm::erase_if(candidates, [&](FusionCandidate &candidate) {
      auto order = readyOrder.find(candidate.resumeTarget);
      if (order == readyOrder.end())
        return true;
      candidate.order = order->second;
      return false;
    });
    if (candidates.size() < 2)
      continue;
    llvm::sort(candidates,
               [](const FusionCandidate &lhs, const FusionCandidate &rhs) {
                 return std::tie(lhs.order, lhs.fragment) <
                        std::tie(rhs.order, rhs.fragment);
               });
    SmallVector<int64_t> fragments;
    llvm::SmallDenseSet<Operation *> functions;
    auto flush = [&] {
      if (fragments.size() >= 2) {
        fusions.push_back(sim::ComputeFusionAttr::get(
            design.getContext(), id++,
            DenseI64ArrayAttr::get(design.getContext(), fragments)));
        ++plannedFusions;
      }
      fragments.clear();
      functions.clear();
    };
    std::optional<uint32_t> previousOrder;
    std::optional<uint32_t> previousEntryOrder;
    for (const FusionCandidate &candidate : candidates) {
      bool adjacent =
          previousOrder && previousEntryOrder &&
          candidate.order == static_cast<uint64_t>(*previousOrder) + 1 &&
          candidate.entryOrder ==
              static_cast<uint64_t>(*previousEntryOrder) + 1;
      if ((!fragments.empty() && !adjacent) ||
          functions.contains(candidate.function))
        flush();
      fragments.push_back(candidate.fragment);
      functions.insert(candidate.function);
      previousOrder = candidate.order;
      previousEntryOrder = candidate.entryOrder;
    }
    flush();
  }
  if (!fusions.empty())
    design->setAttr(metadataName, ArrayAttr::get(design.getContext(), fusions));
}

} // namespace
} // namespace obelisk
