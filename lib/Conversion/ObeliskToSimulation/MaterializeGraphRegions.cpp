//===- MaterializeGraphRegions.cpp - Form coarse compute kernels ---------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

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
  Statistic plannedTriggerGroups{this, "physical-trigger-groups",
                                 "exact physical trigger groups planned"};
  Statistic plannedTier1Kernels{this, "tier1-kernels",
                                "coarse generated clock/eval kernels planned"};
  Statistic plannedTier2Kernels{this, "tier2-kernels",
                                "fine generated convergence kernels planned"};
  Statistic plannedTier3Kernels{this, "tier3-kernels",
                                "bytecode scheduler islands planned"};
  Statistic plannedSharedOwners{this, "shared-owners",
                                "exclusive shared kernel/root owners planned"};
  Statistic plannedPromotionCandidates{this, "promotion-candidates",
                                       "Tier-1 two-state variants planned"};
};

struct PhysicalTriggerKey {
  sim::ComputeResourceKind resource;
  uint64_t descriptor;
  uint64_t low;
  uint64_t width;
  sim::ComputeTriggerKind edge;

  auto tie() const {
    return std::tuple{resource, descriptor, low, width, edge};
  }
  bool operator==(const PhysicalTriggerKey &other) const {
    return tie() == other.tie();
  }
  bool operator<(const PhysicalTriggerKey &other) const {
    return tie() < other.tie();
  }
};

struct RootKey {
  sim::ComputeResourceKind resource;
  uint64_t descriptor;
  uint64_t low;
  uint64_t width;

  auto tie() const { return std::tuple{resource, descriptor, low, width}; }
  bool operator==(const RootKey &other) const { return tie() == other.tie(); }
  bool operator<(const RootKey &other) const { return tie() < other.tie(); }
};

struct InductiveRootKey {
  sim::ComputeResourceKind resource;
  uint64_t descriptor;

  auto tie() const { return std::tuple{resource, descriptor}; }
  bool operator==(const InductiveRootKey &other) const {
    return tie() == other.tie();
  }
  bool operator<(const InductiveRootKey &other) const {
    return tie() < other.tie();
  }
};

struct KernelSpec {
  sim::ComputeRegionKind region = sim::ComputeRegionKind::Active;
  sim::ComputeScheduleKind schedule = sim::ComputeScheduleKind::Acyclic;
  bool loweringReady = false;
  SmallVector<int64_t> fragments;
  uint64_t cost = 0;
  uint32_t lane = 0;
  sim::SchedulerTierKind tier = sim::SchedulerTierKind::Tier3;
  bool twoStateEligible = true;
  SmallVector<PhysicalTriggerKey> triggers;
  SmallVector<RootKey> writes;
  SmallVector<InductiveRootKey> closureRoots;
};

bool isExactDescriptorRange(sim::ComputeEffectAttr effect) {
  return effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
         !effect.getDynamic() && effect.getWidth() != 0 &&
         (effect.getResource() == sim::ComputeResourceKind::Storage ||
          effect.getResource() == sim::ComputeResourceKind::Net);
}

bool isPhysicalTrigger(sim::ComputeEffectAttr effect) {
  return effect.getEffect() == sim::ComputeEffectKind::Watch &&
         isExactDescriptorRange(effect) && !effect.getDeferred() &&
         (effect.getTrigger() == sim::ComputeTriggerKind::Change ||
          effect.getTrigger() == sim::ComputeTriggerKind::Posedge ||
          effect.getTrigger() == sim::ComputeTriggerKind::Negedge ||
          effect.getTrigger() == sim::ComputeTriggerKind::Both);
}

bool isPublishedWrite(sim::ComputeEffectAttr effect) {
  if (!isExactDescriptorRange(effect))
    return false;
  return effect.getEffect() == sim::ComputeEffectKind::Write ||
         effect.getEffect() == sim::ComputeEffectKind::Drive ||
         effect.getEffect() == sim::ComputeEffectKind::NBA;
}

sim::SchedulerTierKind getTier(const KernelSpec &kernel) {
  if (!kernel.loweringReady ||
      kernel.schedule == sim::ComputeScheduleKind::ControlLoop)
    return sim::SchedulerTierKind::Tier3;
  if (kernel.schedule == sim::ComputeScheduleKind::Convergence)
    return sim::SchedulerTierKind::Tier2;
  return sim::SchedulerTierKind::Tier1;
}

uint64_t saturatingAdd(uint64_t lhs, uint64_t rhs) {
  if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
    return std::numeric_limits<uint64_t>::max();
  return lhs + rhs;
}

void ObeliskSimMaterializeGraphRegionsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  design->removeAttr(sim::metadata::computeKernels);
  design->removeAttr(sim::metadata::threeTierSchedule);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError(
        "graph-region materialization requires a verified compute graph");
    return signalPassFailure();
  }

  ArrayAttr nodes = graph.getNodes();
  SmallVector<KernelSpec, 0> kernels;
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
          current.twoStateEligible &= fragment.getTwoState();
          for (Attribute effectAttribute : fragment.getEffects()) {
            auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
            if (isExactDescriptorRange(effect))
              current.closureRoots.push_back(
                  {effect.getResource(), effect.getDescriptor()});
            if (isPhysicalTrigger(effect))
              current.triggers.push_back(
                  {effect.getResource(), effect.getDescriptor(),
                   effect.getLow(), effect.getWidth(), effect.getTrigger()});
            if (isPublishedWrite(effect))
              current.writes.push_back({effect.getResource(),
                                        effect.getDescriptor(), effect.getLow(),
                                        effect.getWidth()});
          }
          continue;
        }
        if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(node)) {
          uint64_t commitCost = commit.getSlots().size() +
                                commit.getAccumulatorSites().size() +
                                commit.getFrontierSites().size();
          current.cost =
              saturatingAdd(current.cost, std::max<uint64_t>(commitCost, 1));
          current.twoStateEligible = false;
          if (isPublishedWrite(commit.getEffect())) {
            sim::ComputeEffectAttr effect = commit.getEffect();
            current.writes.push_back({effect.getResource(),
                                      effect.getDescriptor(), effect.getLow(),
                                      effect.getWidth()});
          }
          continue;
        }
        if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(node)) {
          current.cost = saturatingAdd(
              current.cost, std::max<uint64_t>(commit.getSites().size(), 1));
          current.twoStateEligible = false;
          continue;
        }
        current.loweringReady = false;
        current.twoStateEligible = false;
        current.cost = saturatingAdd(current.cost, 1);
      }
      llvm::sort(current.triggers);
      current.triggers.erase(
          std::unique(current.triggers.begin(), current.triggers.end()),
          current.triggers.end());
      llvm::sort(current.writes);
      current.writes.erase(
          std::unique(current.writes.begin(), current.writes.end()),
          current.writes.end());
      llvm::sort(current.closureRoots);
      current.closureRoots.erase(
          std::unique(current.closureRoots.begin(), current.closureRoots.end()),
          current.closureRoots.end());
      current.tier = getTier(current);
      current.twoStateEligible &=
          current.tier == sim::SchedulerTierKind::Tier1 &&
          current.region == sim::ComputeRegionKind::Active;

      bool merge = hasPending && pending.loweringReady &&
                   current.loweringReady &&
                   pending.schedule == sim::ComputeScheduleKind::Acyclic &&
                   current.schedule == sim::ComputeScheduleKind::Acyclic &&
                   pending.tier == current.tier &&
                   pending.twoStateEligible == current.twoStateEligible &&
                   pending.triggers == current.triggers &&
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
      pending.twoStateEligible &= current.twoStateEligible;
      llvm::append_range(pending.writes, current.writes);
      llvm::sort(pending.writes);
      pending.writes.erase(
          std::unique(pending.writes.begin(), pending.writes.end()),
          pending.writes.end());
      llvm::append_range(pending.closureRoots, current.closureRoots);
      llvm::sort(pending.closureRoots);
      pending.closureRoots.erase(
          std::unique(pending.closureRoots.begin(), pending.closureRoots.end()),
          pending.closureRoots.end());
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

  // The remaining metadata is the revision-coupled ownership ABI consumed by
  // native lowering.  Physical trigger owners come first and are stable under
  // source actor renaming.  Every shared cone/SCC/island then receives one
  // exclusive owner, so no executable fragment is cloned across clock loops.
  SmallVector<PhysicalTriggerKey> triggerKeys;
  for (const KernelSpec &kernel : kernels)
    llvm::append_range(triggerKeys, kernel.triggers);
  llvm::sort(triggerKeys);
  triggerKeys.erase(std::unique(triggerKeys.begin(), triggerKeys.end()),
                    triggerKeys.end());

  SmallVector<Attribute> triggerAttributes;
  for (auto [id, key] : llvm::enumerate(triggerKeys)) {
    auto physical = sim::PhysicalTriggerAttr::get(design.getContext(),
                                                  key.resource, key.descriptor,
                                                  key.low, key.width, key.edge);
    triggerAttributes.push_back(sim::TriggerGroupAttr::get(
        design.getContext(), static_cast<uint32_t>(id), physical));
    ++plannedTriggerGroups;
  }
  auto findTrigger = [&](const PhysicalTriggerKey &key) -> uint32_t {
    return static_cast<uint32_t>(llvm::lower_bound(triggerKeys, key) -
                                 triggerKeys.begin());
  };

  uint32_t ownerCount = static_cast<uint32_t>(triggerKeys.size());
  SmallVector<uint32_t> ownerBits(ownerCount, 0);
  SmallVector<uint32_t> kernelOwners;
  kernelOwners.reserve(kernels.size());
  SmallVector<Attribute> scheduledKernels;
  for (auto [id, kernel] : llvm::enumerate(kernels)) {
    bool shared = kernel.tier != sim::SchedulerTierKind::Tier1 ||
                  kernel.triggers.size() != 1;
    uint32_t owner;
    if (shared) {
      owner = ownerCount++;
      ownerBits.push_back(0);
      ++plannedSharedOwners;
    } else {
      owner = findTrigger(kernel.triggers.front());
    }
    kernelOwners.push_back(owner);
    uint32_t bit = ownerBits[owner]++;
    SmallVector<Attribute> promotionRoots;
    if (kernel.twoStateEligible)
      for (const InductiveRootKey &root : kernel.closureRoots)
        promotionRoots.push_back(sim::InductiveRootAttr::get(
            design.getContext(), root.resource, root.descriptor));
    scheduledKernels.push_back(sim::ScheduledKernelAttr::get(
        design.getContext(), static_cast<uint32_t>(id), owner, bit, kernel.tier,
        kernel.region, kernel.schedule, shared, kernel.loweringReady,
        kernel.twoStateEligible,
        ArrayAttr::get(design.getContext(), promotionRoots),
        DenseI64ArrayAttr::get(design.getContext(), kernel.fragments)));
    switch (kernel.tier) {
    case sim::SchedulerTierKind::Tier1:
      ++plannedTier1Kernels;
      if (kernel.twoStateEligible)
        ++plannedPromotionCandidates;
      break;
    case sim::SchedulerTierKind::Tier2:
      ++plannedTier2Kernels;
      break;
    case sim::SchedulerTierKind::Tier3:
      ++plannedTier3Kernels;
      break;
    }
  }

  // Partition every descriptor at write boundaries.  Each atomic interval is
  // assigned all overlapping writers, so partially overlapping slices from
  // different tiers receive the same lowest-common barrier over precisely the
  // overlap.  Adjacent intervals with identical ownership are folded back
  // together for a compact plan.
  using DescriptorKey = std::pair<sim::ComputeResourceKind, uint64_t>;
  struct WriterRange {
    uint64_t low;
    uint64_t high;
    uint32_t kernel;
  };
  std::map<DescriptorKey, SmallVector<WriterRange>> writesByDescriptor;
  for (auto [kernelID, kernel] : llvm::enumerate(kernels))
    for (const RootKey &root : kernel.writes)
      writesByDescriptor[{root.resource, root.descriptor}].push_back(
          {root.low, root.low + root.width, static_cast<uint32_t>(kernelID)});

  struct OwnedRange {
    sim::ComputeResourceKind resource;
    uint64_t descriptor;
    uint64_t low;
    uint64_t width;
    uint32_t owner;
    sim::SchedulerTierKind tier;
  };
  SmallVector<OwnedRange> ownedRoots;
  for (auto &[descriptor, writes] : writesByDescriptor) {
    std::map<std::vector<uint32_t>, uint32_t> barrierOwners;
    SmallVector<uint64_t> boundaries;
    for (const WriterRange &write : writes) {
      boundaries.push_back(write.low);
      boundaries.push_back(write.high);
    }
    llvm::sort(boundaries);
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    for (auto boundary : llvm::zip(ArrayRef<uint64_t>(boundaries).drop_back(),
                                   ArrayRef<uint64_t>(boundaries).drop_front())) {
      uint64_t low = std::get<0>(boundary);
      uint64_t high = std::get<1>(boundary);
      SmallVector<uint32_t> writers;
      for (const WriterRange &write : writes)
        if (write.low < high && low < write.high)
          writers.push_back(write.kernel);
      llvm::sort(writers);
      writers.erase(std::unique(writers.begin(), writers.end()), writers.end());
      if (writers.empty())
        continue;
      uint32_t owner = kernelOwners[writers.front()];
      sim::SchedulerTierKind tier = kernels[writers.front()].tier;
      bool common = true;
      for (uint32_t writer : ArrayRef<uint32_t>(writers).drop_front()) {
        common &= kernelOwners[writer] == owner;
        if (static_cast<uint32_t>(kernels[writer].tier) >
            static_cast<uint32_t>(tier))
          tier = kernels[writer].tier;
      }
      if (!common) {
        std::vector<uint32_t> writerKey(writers.begin(), writers.end());
        auto [barrier, inserted] =
            barrierOwners.try_emplace(std::move(writerKey), ownerCount);
        if (inserted) {
          ++ownerCount;
          ownerBits.push_back(0);
          ++plannedSharedOwners;
        }
        owner = barrier->second;
      }
      if (!ownedRoots.empty()) {
        OwnedRange &previous = ownedRoots.back();
        if (previous.resource == descriptor.first &&
            previous.descriptor == descriptor.second &&
            previous.low + previous.width == low && previous.owner == owner &&
            previous.tier == tier) {
          previous.width += high - low;
          continue;
        }
      }
      ownedRoots.push_back({descriptor.first, descriptor.second, low,
                            high - low, owner, tier});
    }
  }
  SmallVector<Attribute> roots;
  for (const OwnedRange &root : ownedRoots)
    roots.push_back(sim::ScheduledRootAttr::get(
        design.getContext(), root.resource, root.descriptor, root.low,
        root.width, root.owner, root.tier));

  SmallVector<Attribute> ingress;
  for (auto [kernelID, kernel] : llvm::enumerate(kernels)) {
    auto scheduled = cast<sim::ScheduledKernelAttr>(scheduledKernels[kernelID]);
    for (int64_t fragmentID : kernel.fragments) {
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
          nodes[static_cast<size_t>(fragmentID)]);
      if (!fragment)
        continue;
      SmallVector<uint32_t> fragmentTriggers;
      for (Attribute effectAttribute : fragment.getEffects()) {
        auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
        if (!isPhysicalTrigger(effect))
          continue;
        fragmentTriggers.push_back(findTrigger(
            {effect.getResource(), effect.getDescriptor(), effect.getLow(),
             effect.getWidth(), effect.getTrigger()}));
      }
      llvm::sort(fragmentTriggers);
      fragmentTriggers.erase(
          std::unique(fragmentTriggers.begin(), fragmentTriggers.end()),
          fragmentTriggers.end());
      for (uint32_t trigger : fragmentTriggers)
        ingress.push_back(sim::SchedulerIngressAttr::get(
            design.getContext(), trigger, scheduled.getOwner(),
            scheduled.getReadyBit(), static_cast<uint32_t>(fragmentID)));
    }
  }

  design->setAttr(sim::metadata::threeTierSchedule,
                  sim::ThreeTierScheduleAttr::get(
                      design.getContext(), sim::metadata::schemaVersion, graph,
                      ownerCount,
                      ArrayAttr::get(design.getContext(), triggerAttributes),
                      ArrayAttr::get(design.getContext(), scheduledKernels),
                      ArrayAttr::get(design.getContext(), roots),
                      ArrayAttr::get(design.getContext(), ingress)));
}

} // namespace
} // namespace obelisk
