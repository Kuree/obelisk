//===- MaterializeGraphRegions.cpp - Form coarse compute kernels ---------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZEGRAPHREGIONSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimMaterializeGraphRegionsPass final
    : public impl::ObeliskSimMaterializeGraphRegionsPassBase<
          ObeliskSimMaterializeGraphRegionsPass> {
public:
  using Base::Base;
  void runOnOperation() override;

private:
  Statistic materializedKernels{this, "materialized-kernels",
                                "coarse graph-region kernels materialized"};
  Statistic loweringReadyKernels{this, "lowering-ready-kernels",
                                 "kernels ready for generated native lowering"};
  Statistic mergedGroups{this, "merged-groups",
                         "acyclic schedule groups merged into kernels"};
  Statistic loweringReadyFragments{
      this, "lowering-ready-fragments",
      "fine fragments covered by lowering-ready kernels"};
  Statistic fallbackFragments{this, "fallback-fragments",
                              "fine fragments retained at fallback boundaries"};
};

struct KernelSpec {
  sim::ComputeRegionKind region = sim::ComputeRegionKind::Active;
  sim::ComputeScheduleKind schedule = sim::ComputeScheduleKind::Acyclic;
  bool loweringReady = false;
  SmallVector<int64_t> fragments;
  uint64_t cost = 0;
  uint32_t lane = 0;
};

uint64_t saturatingAdd(uint64_t lhs, uint64_t rhs) {
  if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
    return std::numeric_limits<uint64_t>::max();
  return lhs + rhs;
}

void ObeliskSimMaterializeGraphRegionsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  design->removeAttr(sim::metadata::computeKernels);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError(
        "graph-region materialization requires a verified compute graph");
    return signalPassFailure();
  }

  ArrayAttr nodes = graph.getNodes();
  SmallVector<KernelSpec> kernels;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    KernelSpec pending{};
    bool hasPending = false;
    auto flush = [&] {
      if (!hasPending)
        return;
      kernels.push_back(std::move(pending));
      pending = KernelSpec{};
      hasPending = false;
    };
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      KernelSpec current{region.getKind(), group.getSchedule()};
      current.loweringReady =
          group.getSchedule() != sim::ComputeScheduleKind::ControlLoop;
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size()) {
          design.emitOpError("schedule group references an invalid node");
          return signalPassFailure();
        }
        current.fragments.push_back(member);
        Attribute node = nodes[static_cast<size_t>(member)];
        if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(node)) {
          current.cost = saturatingAdd(current.cost, fragment.getCost());
          if (fragment.getTier() != sim::ComputeTierKind::Native)
            current.loweringReady = false;
          continue;
        }
        if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(node)) {
          uint64_t commitCost = commit.getSlots().size() +
                                commit.getAccumulatorSites().size() +
                                commit.getFrontierSites().size();
          current.cost =
              saturatingAdd(current.cost, std::max<uint64_t>(commitCost, 1));
          continue;
        }
        if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(node)) {
          current.cost = saturatingAdd(
              current.cost, std::max<uint64_t>(commit.getSites().size(), 1));
          continue;
        }
        current.loweringReady = false;
        current.cost = saturatingAdd(current.cost, 1);
      }

      bool merge = hasPending && pending.loweringReady &&
                   current.loweringReady &&
                   pending.schedule == sim::ComputeScheduleKind::Acyclic &&
                   current.schedule == sim::ComputeScheduleKind::Acyclic &&
                   current.cost <= maxAcyclicCost &&
                   pending.cost <= maxAcyclicCost - current.cost;
      if (!merge) {
        flush();
        pending = std::move(current);
        hasPending = true;
        continue;
      }
      pending.fragments.append(current.fragments.begin(),
                               current.fragments.end());
      pending.cost += current.cost;
      ++mergedGroups;
    }
    flush();
  }

  SmallVector<uint64_t> laneCosts(graph.getWorkers(), 0);
  SmallVector<unsigned> placementOrder;
  for (auto [index, kernel] : llvm::enumerate(kernels))
    if (kernel.loweringReady)
      placementOrder.push_back(index);
  llvm::stable_sort(placementOrder, [&](unsigned lhs, unsigned rhs) {
    return kernels[lhs].cost > kernels[rhs].cost;
  });
  for (unsigned index : placementOrder) {
    uint32_t lane = static_cast<uint32_t>(
        std::min_element(laneCosts.begin(), laneCosts.end()) -
        laneCosts.begin());
    kernels[index].lane = lane;
    laneCosts[lane] = saturatingAdd(laneCosts[lane], kernels[index].cost);
  }

  SmallVector<Attribute> attributes;
  attributes.reserve(kernels.size());
  for (auto [id, kernel] : llvm::enumerate(kernels)) {
    attributes.push_back(sim::ComputeKernelAttr::get(
        design.getContext(), static_cast<uint32_t>(id), kernel.region,
        kernel.schedule, kernel.lane, kernel.cost, kernel.loweringReady,
        DenseI64ArrayAttr::get(design.getContext(), kernel.fragments)));
    ++materializedKernels;
    if (kernel.loweringReady) {
      ++loweringReadyKernels;
      loweringReadyFragments += kernel.fragments.size();
    } else {
      fallbackFragments += kernel.fragments.size();
    }
  }
  design->setAttr(sim::metadata::computeKernels,
                  ArrayAttr::get(design.getContext(), attributes));
}

} // namespace
} // namespace obelisk
