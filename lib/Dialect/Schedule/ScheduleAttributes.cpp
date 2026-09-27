// Invariants for the canonical scheduling attributes.
#include "mlir/IR/Diagnostics.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include <limits>
#include <map>
#include <set>
using namespace mlir;
namespace obelisk::schedule {
static LogicalResult
verifyEffectArray(llvm::function_ref<InFlightDiagnostic()> emitError,
                  ArrayAttr effects, StringRef owner) {
  if (!effects)
    return emitError() << owner << " requires an effect array";
  if (llvm::any_of(effects, [](Attribute attr) {
        return !isa<ComputeEffectAttr>(attr);
      }))
    return emitError() << owner << " contains a non-effect attribute";
  return success();
}

LogicalResult ComputeEffectAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ComputeEffectKind effect, ComputeResourceKind resource,
    ComputeTargetKind target, uint64_t descriptor, uint32_t formal,
    uint64_t low, uint64_t width, bool dynamic, bool deferred,
    ComputeTriggerKind trigger) {
  if (target != ComputeTargetKind::Descriptor && descriptor != 0)
    return emitError() << "non-descriptor effect has a descriptor value";
  if (target != ComputeTargetKind::Formal && formal != 0)
    return emitError() << "non-formal effect has a formal index";
  if (resource == ComputeResourceKind::Unknown &&
      target != ComputeTargetKind::Unknown)
    return emitError() << "unknown effect has a concrete target";
  if (resource == ComputeResourceKind::Local &&
      target != ComputeTargetKind::Local)
    return emitError() << "local effect has a non-local target";
  if (resource != ComputeResourceKind::Unknown &&
      resource != ComputeResourceKind::Local &&
      target != ComputeTargetKind::Descriptor &&
      target != ComputeTargetKind::Formal)
    return emitError() << "concrete effect has no descriptor or formal target";
  if (resource == ComputeResourceKind::Unknown &&
      (low != 0 || width != 0 || dynamic))
    return emitError() << "unknown effect must not claim a concrete range";
  if (resource != ComputeResourceKind::Unknown && width == 0)
    return emitError() << "concrete effect has zero width";
  if (width != 0 && low > UINT64_MAX - width)
    return emitError() << "effect packed range overflows";
  bool watches = effect == ComputeEffectKind::Watch;
  if (watches != (trigger != ComputeTriggerKind::None))
    return emitError() << "watch effects require exactly one trigger kind";
  if (deferred && effect != ComputeEffectKind::NBA &&
      effect != ComputeEffectKind::Trigger)
    return emitError() << "only NBA and trigger effects may be deferred";
  return success();
}

LogicalResult ComputeFragmentAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    FlatSymbolRefAttr function, uint32_t block, ComputeRegionKind region,
    ComputeActionKind action, ComputeTierKind tier, uint64_t cost,
    uint32_t lane, bool twoState, ArrayAttr effects) {
  if (!function)
    return emitError() << "fragment requires a function symbol";
  return verifyEffectArray(emitError, effects, "fragment");
}

LogicalResult ComputeNBACommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr slots, DenseI64ArrayAttr accumulatorSites,
    DenseI64ArrayAttr frontierSites, ComputeEffectAttr effect) {
  if (!slots || !accumulatorSites || !frontierSites || !effect ||
      effect.getEffect() != ComputeEffectKind::Write)
    return emitError()
           << "NBA commit requires staging inventories and one write effect";
  if (slots.empty() && accumulatorSites.empty() && frontierSites.empty())
    return emitError() << "NBA commit requires at least one site";
  llvm::SmallDenseSet<int64_t> sites;
  for (DenseI64ArrayAttr inventory : {slots, accumulatorSites, frontierSites})
    for (int64_t site : inventory.asArrayRef())
      if (site < 0 || !sites.insert(site).second)
        return emitError() << "NBA commit has an invalid or duplicate site";
  return success();
}

LogicalResult ComputeEventCommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr sites, ComputeEffectAttr effect) {
  if (!sites || !effect || effect.getEffect() != ComputeEffectKind::Trigger ||
      !effect.getDeferred())
    return emitError()
           << "event commit requires sites and one deferred trigger effect";
  if (sites.empty())
    return emitError() << "event commit requires at least one site";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t site : sites.asArrayRef())
    if (site < 0 || !unique.insert(site).second)
      return emitError() << "event commit has an invalid or duplicate site";
  return success();
}

LogicalResult
ComputeEdgeAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t source, uint32_t target, ComputeEdgeKind kind,
                        ComputeEffectAttr resource) {
  bool needsResource = kind == ComputeEdgeKind::Sensitivity ||
                       kind == ComputeEdgeKind::NBAStage ||
                       kind == ComputeEdgeKind::NBAActivate ||
                       kind == ComputeEdgeKind::Conflict ||
                       kind == ComputeEdgeKind::DeferredStage ||
                       kind == ComputeEdgeKind::DeferredActivate;
  if (needsResource && !resource)
    return emitError() << "edge kind requires a resource effect";
  if ((kind == ComputeEdgeKind::ProcessOrder ||
       kind == ComputeEdgeKind::Resume || kind == ComputeEdgeKind::Spawn) &&
      resource)
    return emitError() << "control-only edge cannot carry a resource";
  return success();
}

LogicalResult
ComputeGroupAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         DenseI64ArrayAttr fragments,
                         ComputeScheduleKind schedule, ArrayAttr feedback) {
  if (!fragments || fragments.empty() || !feedback)
    return emitError() << "schedule group must contain fragments and feedback";
  llvm::SmallDenseSet<int64_t> members;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !members.insert(fragment).second)
      return emitError() << "schedule group has an invalid or duplicate member";
  if (failed(verifyEffectArray(emitError, feedback, "schedule feedback")))
    return failure();
  if (schedule != ComputeScheduleKind::Convergence && !feedback.empty())
    return emitError() << "only convergence groups may carry feedback";
  return success();
}

LogicalResult
ComputeFusionAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint32_t id, DenseI64ArrayAttr fragments) {
  (void)id;
  if (!fragments || fragments.size() < 2)
    return emitError() << "compute fusion requires at least two fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "compute fusion has an invalid or duplicate fragment";
  return success();
}

LogicalResult ComputeKernelAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    ComputeRegionKind region, ComputeScheduleKind schedule, uint32_t lane,
    uint64_t cost, bool loweringReady, DenseI64ArrayAttr fragments) {
  (void)id;
  (void)region;
  (void)lane;
  (void)cost;
  if (!fragments || fragments.empty())
    return emitError() << "compute kernel must contain fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "compute kernel has an invalid or duplicate fragment";
  if (loweringReady && schedule == ComputeScheduleKind::ControlLoop)
    return emitError() << "control-loop kernel cannot be lowering-ready";
  return success();
}

LogicalResult
PhysicalTriggerAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            ComputeResourceKind resource, uint64_t descriptor,
                            uint64_t low, uint64_t width,
                            ComputeTriggerKind edge) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "physical trigger requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "physical trigger has an invalid packed range";
  if (edge != ComputeTriggerKind::Change &&
      edge != ComputeTriggerKind::Posedge &&
      edge != ComputeTriggerKind::Negedge && edge != ComputeTriggerKind::Both)
    return emitError() << "physical trigger requires an exact edge kind";
  return success();
}

LogicalResult
TriggerGroupAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         uint32_t id, PhysicalTriggerAttr key) {
  (void)id;
  if (!key)
    return emitError() << "trigger group has no physical key";
  return success();
}

LogicalResult
InductiveRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeResourceKind resource, uint64_t descriptor) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "inductive root requires storage or net state";
  return success();
}

LogicalResult ScheduledKernelAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    uint32_t owner, uint32_t readyBit, SchedulerTierKind tier,
    ComputeRegionKind region, ComputeScheduleKind schedule, bool shared,
    bool loweringReady, bool twoStateEligible, ArrayAttr promotionRoots,
    DenseI64ArrayAttr fragments) {
  (void)id;
  (void)owner;
  (void)readyBit;
  (void)region;
  (void)shared;
  if (!fragments || fragments.empty())
    return emitError() << "scheduled kernel must retain original fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "scheduled kernel has an invalid or duplicate fragment";
  if (tier == SchedulerTierKind::Tier1 &&
      schedule != ComputeScheduleKind::Acyclic)
    return emitError() << "Tier-1 kernel must be acyclic";
  if (tier == SchedulerTierKind::Tier2 &&
      schedule == ComputeScheduleKind::ControlLoop)
    return emitError() << "control loops require Tier 3";
  if (tier == SchedulerTierKind::Tier3 && loweringReady)
    return emitError() << "Tier-3 kernel cannot be lowering-ready";
  if (tier != SchedulerTierKind::Tier3 && !loweringReady)
    return emitError() << "generated kernel must be lowering-ready";
  if (twoStateEligible && (tier != SchedulerTierKind::Tier1 || !loweringReady))
    return emitError()
           << "only lowering-ready Tier-1 kernels may have two-state bodies";
  if (!promotionRoots)
    return emitError() << "scheduled kernel has no promotion-root inventory";
  llvm::SmallDenseSet<Attribute, 8> roots;
  for (Attribute root : promotionRoots)
    if (!isa<InductiveRootAttr>(root) || !roots.insert(root).second)
      return emitError() << "promotion-root inventory is invalid or duplicated";
  if (!twoStateEligible && !promotionRoots.empty())
    return emitError()
           << "kernel without a two-state body cannot have promotion roots";
  return success();
}

LogicalResult
SchedulerIngressAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                             uint32_t trigger, uint32_t owner,
                             uint32_t readyBit, uint32_t fragment) {
  (void)emitError;
  (void)trigger;
  (void)owner;
  (void)readyBit;
  (void)fragment;
  return success();
}

LogicalResult
ScheduledRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeResourceKind resource, uint64_t descriptor,
                          uint64_t low, uint64_t width, uint32_t owner,
                          SchedulerTierKind tier) {
  (void)descriptor;
  (void)owner;
  (void)tier;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "scheduled root requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "scheduled root has an invalid packed range";
  return success();
}

LogicalResult
ClockKeyAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     ComputeResourceKind resource, uint64_t descriptor,
                     uint64_t low, uint64_t width, ComputeTriggerKind edge) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "clock key requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "clock key has an invalid packed range";
  if (edge != ComputeTriggerKind::Change &&
      edge != ComputeTriggerKind::Posedge &&
      edge != ComputeTriggerKind::Negedge && edge != ComputeTriggerKind::Both)
    return emitError() << "clock key requires an exact change or edge kind";
  return success();
}

LogicalResult
ClockKernelAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t id, ClockKeyAttr key) {
  (void)id;
  if (!key)
    return emitError() << "clock kernel has no physical key";
  return success();
}

LogicalResult
MergedFragmentAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           uint32_t id, uint32_t owner, uint32_t bit,
                           bool shared, bool loweringReady,
                           DenseI64ArrayAttr fragments) {
  (void)id;
  (void)owner;
  (void)bit;
  (void)shared;
  (void)loweringReady;
  if (!fragments || fragments.empty())
    return emitError() << "merged fragment must retain original fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "merged fragment has an invalid or duplicate original ID";
  return success();
}

LogicalResult
KernelIngressAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint32_t clock, uint32_t owner, uint32_t bit,
                          uint32_t fragment) {
  (void)clock;
  (void)owner;
  (void)bit;
  (void)fragment;
  return success();
}

LogicalResult
ClockKernelPlanAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint32_t version, ComputeGraphAttr sourceGraph,
                            uint32_t ownerCount, ArrayAttr clocks,
                            ArrayAttr mergedFragments, ArrayAttr ingress) {
  if (version != schedule::metadata::schemaVersion || !sourceGraph)
    return emitError() << "invalid clock-kernel plan version or source graph";
  if (!clocks || !mergedFragments || !ingress)
    return emitError() << "clock-kernel plan inventory is absent";
  if (ownerCount < clocks.size())
    return emitError() << "clock-kernel plan has fewer owners than clocks";

  llvm::SmallDenseSet<Attribute, 16> keys;
  for (auto [index, attribute] : llvm::enumerate(clocks)) {
    auto clock = dyn_cast<ClockKernelAttr>(attribute);
    if (!clock || clock.getId() != index || !keys.insert(clock.getKey()).second)
      return emitError() << "clock-kernel inventory is invalid or duplicated";
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ownerBits;
  llvm::SmallDenseSet<int64_t, 32> originalFragments;
  for (auto [index, attribute] : llvm::enumerate(mergedFragments)) {
    auto merged = dyn_cast<MergedFragmentAttr>(attribute);
    if (!merged || merged.getId() != index || merged.getOwner() >= ownerCount ||
        !ownerBits.insert({merged.getOwner(), merged.getBit()}).second)
      return emitError()
             << "merged-fragment ownership is invalid or duplicated";
    if (merged.getShared() != (merged.getOwner() >= clocks.size()))
      return emitError() << "merged-fragment shared ownership is inconsistent";
    for (int64_t fragment : merged.getFragments().asArrayRef())
      if (static_cast<uint64_t>(fragment) >= sourceGraph.getNodes().size() ||
          !originalFragments.insert(fragment).second)
        return emitError()
               << "original fragment has no unique merged-fragment owner";
  }
  if (originalFragments.size() != sourceGraph.getNodes().size())
    return emitError() << "clock-kernel plan does not own every graph node";

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ingressKeys;
  for (Attribute attribute : ingress) {
    auto mapping = dyn_cast<KernelIngressAttr>(attribute);
    if (!mapping || mapping.getClock() >= clocks.size() ||
        mapping.getOwner() >= ownerCount ||
        !ownerBits.contains({mapping.getOwner(), mapping.getBit()}) ||
        mapping.getFragment() >= sourceGraph.getNodes().size() ||
        !ingressKeys.insert({mapping.getClock(), mapping.getFragment()}).second)
      return emitError()
             << "clock-kernel ingress mapping is invalid or duplicated";
  }
  return success();
}

LogicalResult
ComputeRegionAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeRegionKind kind, ArrayAttr groups) {
  if (!groups || llvm::any_of(groups, [](Attribute attr) {
        return !isa<ComputeGroupAttr>(attr);
      }))
    return emitError() << "event region contains a non-group attribute";
  return success();
}

LogicalResult
ComputeGraphAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         uint32_t version, ComputeVPIMode vpi, uint32_t workers,
                         ArrayAttr nodes, ArrayAttr edges, ArrayAttr regions) {
  if (version != schedule::metadata::schemaVersion)
    return emitError() << "unsupported compute-graph version";
  if (workers == 0 || workers > 65535)
    return emitError() << "worker count is outside the lane ID range";
  if (!nodes || llvm::any_of(nodes, [](Attribute attr) {
        return !isa<ComputeFragmentAttr, ComputeNBACommitAttr,
                    ComputeEventCommitAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-node attribute";
  if (!edges || llvm::any_of(edges, [](Attribute attr) {
        return !isa<ComputeEdgeAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-edge attribute";
  if (!regions || regions.size() != 5)
    return emitError() << "compute graph requires all five event regions";
  static constexpr ComputeRegionKind expectedRegions[] = {
      ComputeRegionKind::Active, ComputeRegionKind::NBA,
      ComputeRegionKind::Observed, ComputeRegionKind::Reactive,
      ComputeRegionKind::Postponed};
  for (auto [attribute, expected] : llvm::zip(regions, expectedRegions)) {
    auto region = dyn_cast<ComputeRegionAttr>(attribute);
    if (!region || region.getKind() != expected)
      return emitError() << "compute graph event regions are out of order";
  }
  return success();
}

LogicalResult ThreeTierScheduleAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t version,
    ComputeGraphAttr sourceGraph, uint32_t ownerCount, ArrayAttr triggers,
    ArrayAttr kernels, ArrayAttr roots, ArrayAttr ingress) {
  if (version != schedule::metadata::schemaVersion || !sourceGraph)
    return emitError() << "invalid three-tier schedule version or source graph";
  if (!triggers || !kernels || !roots || !ingress)
    return emitError() << "three-tier schedule inventory is absent";
  if (ownerCount < triggers.size())
    return emitError() << "three-tier schedule has fewer owners than triggers";

  llvm::SmallDenseSet<Attribute, 16> keys;
  for (auto [index, attribute] : llvm::enumerate(triggers)) {
    auto trigger = dyn_cast<TriggerGroupAttr>(attribute);
    if (!trigger || trigger.getId() != index ||
        !keys.insert(trigger.getKey()).second)
      return emitError() << "trigger-group inventory is invalid or duplicated";
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ownerBits;
  llvm::SmallDenseSet<int64_t, 32> originalFragments;
  llvm::DenseMap<uint32_t, std::pair<uint32_t, uint32_t>> fragmentOwners;
  llvm::DenseMap<uint32_t, ScheduledKernelAttr> fragmentKernels;
  uint32_t nextSharedOwner = static_cast<uint32_t>(triggers.size());
  for (auto [index, attribute] : llvm::enumerate(kernels)) {
    auto kernel = dyn_cast<ScheduledKernelAttr>(attribute);
    if (!kernel || kernel.getId() != index || kernel.getOwner() >= ownerCount ||
        !ownerBits.insert({kernel.getOwner(), kernel.getReadyBit()}).second)
      return emitError() << "kernel ownership is invalid or duplicated";
    if (kernel.getShared() != (kernel.getOwner() >= triggers.size()))
      return emitError() << "kernel shared ownership is inconsistent";
    if (kernel.getShared() && kernel.getOwner() != nextSharedOwner++)
      return emitError() << "shared kernel owners are not canonical";
    if (!kernel.getShared() && kernel.getTier() != SchedulerTierKind::Tier1)
      return emitError() << "physical-trigger owners must remain in Tier 1";
    for (int64_t fragment : kernel.getFragments().asArrayRef()) {
      if (static_cast<uint64_t>(fragment) >= sourceGraph.getNodes().size() ||
          !originalFragments.insert(fragment).second)
        return emitError() << "graph node has no unique scheduled-kernel owner";
      fragmentOwners.try_emplace(static_cast<uint32_t>(fragment),
                                 kernel.getOwner(), kernel.getReadyBit());
      fragmentKernels.try_emplace(static_cast<uint32_t>(fragment), kernel);
    }
  }
  if (originalFragments.size() != sourceGraph.getNodes().size())
    return emitError() << "three-tier schedule does not own every graph node";

  llvm::SmallDenseSet<
      std::tuple<ComputeResourceKind, uint64_t, uint64_t, uint64_t>, 16>
      rootKeys;
  SmallVector<ScheduledRootAttr> scheduledRoots;
  for (Attribute attribute : roots) {
    auto root = dyn_cast<ScheduledRootAttr>(attribute);
    if (!root || root.getOwner() >= ownerCount ||
        !rootKeys
             .insert({root.getResource(), root.getDescriptor(), root.getLow(),
                      root.getWidth()})
             .second)
      return emitError() << "scheduled-root ownership is invalid or duplicated";
    scheduledRoots.push_back(root);
  }

  struct ScheduledWriter {
    ComputeResourceKind resource;
    uint64_t descriptor;
    uint64_t low;
    uint64_t high;
    uint32_t kernel;
  };
  SmallVector<ScheduledWriter> writers;
  auto recordWriter = [&](uint32_t fragmentID, ComputeEffectAttr effect) {
    if (!effect || effect.getTarget() != ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getWidth() == 0 ||
        (effect.getResource() != ComputeResourceKind::Storage &&
         effect.getResource() != ComputeResourceKind::Net) ||
        (effect.getEffect() != ComputeEffectKind::Write &&
         effect.getEffect() != ComputeEffectKind::Drive &&
         effect.getEffect() != ComputeEffectKind::NBA))
      return;
    auto kernel = fragmentKernels.find(fragmentID);
    if (kernel == fragmentKernels.end())
      return;
    writers.push_back({effect.getResource(), effect.getDescriptor(),
                       effect.getLow(), effect.getLow() + effect.getWidth(),
                       kernel->second.getId()});
  };
  for (auto [fragmentID, node] : llvm::enumerate(sourceGraph.getNodes())) {
    if (auto fragment = dyn_cast<ComputeFragmentAttr>(node))
      for (Attribute effect : fragment.getEffects())
        recordWriter(static_cast<uint32_t>(fragmentID),
                     cast<ComputeEffectAttr>(effect));
    else if (auto commit = dyn_cast<ComputeNBACommitAttr>(node))
      recordWriter(static_cast<uint32_t>(fragmentID), commit.getEffect());
  }
  llvm::sort(scheduledRoots, [](ScheduledRootAttr lhs, ScheduledRootAttr rhs) {
    return std::tuple(lhs.getResource(), lhs.getDescriptor(), lhs.getLow(),
                      lhs.getWidth()) <
           std::tuple(rhs.getResource(), rhs.getDescriptor(), rhs.getLow(),
                      rhs.getWidth());
  });
  using DescriptorKey = std::pair<ComputeResourceKind, uint64_t>;
  // The planner may coalesce adjacent intervals with the same ownership even
  // when their individual writers differ. Verify every atomic interval, not
  // whether each overlapping write spans the entire coalesced root. Sorted
  // endpoint sweeps also avoid comparing every root with every design writer.
  struct RangeEvent {
    uint64_t position;
    uint32_t identity;
    bool root;
    bool start;
  };
  std::map<DescriptorKey, SmallVector<RangeEvent>> eventsByDescriptor;
  for (const ScheduledWriter &writer : writers) {
    auto &events = eventsByDescriptor[{writer.resource, writer.descriptor}];
    events.push_back({writer.low, writer.kernel, false, true});
    events.push_back({writer.high, writer.kernel, false, false});
  }
  for (auto [index, root] : llvm::enumerate(scheduledRoots)) {
    auto &events =
        eventsByDescriptor[{root.getResource(), root.getDescriptor()}];
    events.push_back({root.getLow(), static_cast<uint32_t>(index), true, true});
    events.push_back({root.getLow() + root.getWidth(),
                      static_cast<uint32_t>(index), true, false});
  }
  uint32_t nextBarrierOwner = nextSharedOwner;
  for (auto &[descriptor, events] : eventsByDescriptor) {
    llvm::sort(events, [](const RangeEvent &lhs, const RangeEvent &rhs) {
      return std::tie(lhs.position, lhs.start, lhs.root, lhs.identity) <
             std::tie(rhs.position, rhs.start, rhs.root, rhs.identity);
    });
    // Counts preserve overlapping or repeated effects from the same kernel.
    // Ending one write must not remove another write that is still active.
    std::map<uint32_t, uint32_t> activeWriters;
    std::set<uint32_t> activeRoots;
    std::map<std::vector<uint32_t>, uint32_t> barrierOwners;
    size_t next = 0;
    while (next < events.size()) {
      uint64_t position = events[next].position;
      do {
        const RangeEvent &event = events[next++];
        if (event.root) {
          if (event.start)
            activeRoots.insert(event.identity);
          else
            activeRoots.erase(event.identity);
        } else if (event.start) {
          ++activeWriters[event.identity];
        } else {
          auto writer = activeWriters.find(event.identity);
          assert(writer != activeWriters.end());
          if (--writer->second == 0)
            activeWriters.erase(writer);
        }
      } while (next < events.size() && events[next].position == position);
      if (next == events.size())
        break;
      if (activeRoots.size() > 1)
        return emitError() << "scheduled-root ranges overlap";
      if (activeWriters.empty()) {
        if (!activeRoots.empty())
          return emitError()
                 << "scheduled root crosses or lacks an exact writer partition";
        continue;
      }
      if (activeRoots.empty())
        return emitError() << "scheduled roots do not cover every writer range";
      auto first =
          cast<ScheduledKernelAttr>(kernels[activeWriters.begin()->first]);
      SchedulerTierKind tier = first.getTier();
      uint32_t expectedOwner = first.getOwner();
      bool common = true;
      std::vector<uint32_t> writerKey;
      for (auto [kernelID, count] : activeWriters) {
        auto kernel = cast<ScheduledKernelAttr>(kernels[kernelID]);
        tier = std::max(tier, kernel.getTier());
        common &= kernel.getOwner() == expectedOwner;
        writerKey.push_back(kernelID);
      }
      if (!common) {
        auto [barrier, inserted] =
            barrierOwners.try_emplace(std::move(writerKey), nextBarrierOwner);
        if (inserted)
          ++nextBarrierOwner;
        expectedOwner = barrier->second;
      }
      ScheduledRootAttr root = scheduledRoots[*activeRoots.begin()];
      if (root.getTier() != tier || root.getOwner() != expectedOwner)
        return emitError()
               << "scheduled-root owner or tier disagrees with writers";
    }
  }
  if (nextBarrierOwner != ownerCount)
    return emitError() << "three-tier owner inventory is not canonical";

  auto isPhysicalWatch = [](ComputeEffectAttr effect) {
    return effect.getEffect() == ComputeEffectKind::Watch &&
           effect.getTarget() == ComputeTargetKind::Descriptor &&
           !effect.getDynamic() && !effect.getDeferred() &&
           effect.getWidth() != 0 &&
           (effect.getResource() == ComputeResourceKind::Storage ||
            effect.getResource() == ComputeResourceKind::Net) &&
           (effect.getTrigger() == ComputeTriggerKind::Change ||
            effect.getTrigger() == ComputeTriggerKind::Posedge ||
            effect.getTrigger() == ComputeTriggerKind::Negedge ||
            effect.getTrigger() == ComputeTriggerKind::Both);
  };
  auto findTrigger = [&](ComputeEffectAttr effect) -> std::optional<uint32_t> {
    for (auto [index, attribute] : llvm::enumerate(triggers)) {
      auto key = cast<TriggerGroupAttr>(attribute).getKey();
      if (key.getResource() == effect.getResource() &&
          key.getDescriptor() == effect.getDescriptor() &&
          key.getLow() == effect.getLow() &&
          key.getWidth() == effect.getWidth() &&
          key.getEdge() == effect.getTrigger())
        return static_cast<uint32_t>(index);
    }
    return std::nullopt;
  };

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> requiredIngress;
  for (auto [fragmentID, node] : llvm::enumerate(sourceGraph.getNodes())) {
    auto fragment = dyn_cast<ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<ComputeEffectAttr>(effectAttribute);
      if (!isPhysicalWatch(effect))
        continue;
      std::optional<uint32_t> trigger = findTrigger(effect);
      if (!trigger)
        return emitError()
               << "physical watch is absent from the trigger inventory";
      requiredIngress.insert({*trigger, static_cast<uint32_t>(fragmentID)});
    }
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ingressKeys;
  for (Attribute attribute : ingress) {
    auto mapping = dyn_cast<SchedulerIngressAttr>(attribute);
    auto owner = mapping ? fragmentOwners.find(mapping.getFragment())
                         : fragmentOwners.end();
    if (!mapping || mapping.getTrigger() >= triggers.size() ||
        mapping.getOwner() >= ownerCount ||
        !ownerBits.contains({mapping.getOwner(), mapping.getReadyBit()}) ||
        mapping.getFragment() >= sourceGraph.getNodes().size() ||
        owner == fragmentOwners.end() ||
        owner->second != std::pair(mapping.getOwner(), mapping.getReadyBit()) ||
        !requiredIngress.contains(
            {mapping.getTrigger(), mapping.getFragment()}) ||
        !ingressKeys.insert({mapping.getTrigger(), mapping.getFragment()})
             .second)
      return emitError() << "scheduler ingress is invalid or duplicated";
  }
  if (ingressKeys.size() != requiredIngress.size())
    return emitError()
           << "scheduler ingress does not cover every physical watch";
  return success();
}

LogicalResult
StaticStateRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint64_t descriptor, uint32_t width, bool direct,
                            bool guarded, bool nba) {
  (void)descriptor;
  if (direct && guarded)
    return emitError() << "static state root cannot be both direct and guarded";
  if ((direct || guarded || nba) && width == 0)
    return emitError() << "specialized static state root has zero width";
  return success();
}

LogicalResult
StaticActorRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            FlatSymbolRefAttr function, uint64_t descriptor,
                            bool read, bool write) {
  (void)descriptor;
  if (!function)
    return emitError() << "static actor/root dependency has no actor";
  if (!read && !write)
    return emitError() << "static actor/root dependency has no access kind";
  return success();
}

LogicalResult StaticSpecializationAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t version,
    uint32_t maxPackedWidth, ComputeGraphAttr sourceGraph, ArrayAttr roots,
    ArrayAttr actorRoots, DenseI64ArrayAttr nbaRoots) {
  if (version != schedule::metadata::schemaVersion || maxPackedWidth == 0 ||
      maxPackedWidth > schedule::metadata::maxDirectStaticStateBits ||
      !sourceGraph)
    return emitError() << "invalid static-specialization version or width";
  if (!roots || !actorRoots || !nbaRoots)
    return emitError() << "static-specialization inventory is absent";

  DenseMap<uint64_t, StaticStateRootAttr> rootByDescriptor;
  for (Attribute attribute : roots) {
    auto root = dyn_cast<StaticStateRootAttr>(attribute);
    if (!root)
      return emitError()
             << "static-specialization root array has an invalid element";
    if (!rootByDescriptor.try_emplace(root.getDescriptor(), root).second)
      return emitError() << "static-specialization root is duplicated";
  }

  llvm::SmallDenseSet<std::pair<Attribute, uint64_t>, 16> dependencies;
  for (Attribute attribute : actorRoots) {
    auto dependency = dyn_cast<StaticActorRootAttr>(attribute);
    if (!dependency)
      return emitError() << "static actor/root array has an invalid element";
    auto root = rootByDescriptor.find(dependency.getDescriptor());
    if (root == rootByDescriptor.end() ||
        (!root->second.getDirect() && !root->second.getGuarded()))
      return emitError()
             << "static actor/root dependency references a generic root";
    if (!dependencies
             .insert({dependency.getFunction(), dependency.getDescriptor()})
             .second)
      return emitError() << "static actor/root dependency is duplicated";
  }

  llvm::SmallDenseSet<uint64_t, 16> orderedNBARoots;
  for (int64_t descriptor : nbaRoots.asArrayRef()) {
    if (descriptor < 0 ||
        !orderedNBARoots.insert(static_cast<uint64_t>(descriptor)).second)
      return emitError()
             << "static NBA root inventory has an invalid or duplicate root";
    auto root = rootByDescriptor.find(static_cast<uint64_t>(descriptor));
    if (root == rootByDescriptor.end() || !root->second.getNba())
      return emitError()
             << "static NBA root inventory references a generic root";
  }
  for (const auto &entry : rootByDescriptor)
    if (entry.second.getNba() && !orderedNBARoots.contains(entry.first))
      return emitError()
             << "static NBA root policy is absent from the ordered inventory";
  return success();
}

LogicalResult
StaticSuperstepAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint32_t version, ComputeGraphAttr sourceGraph,
                            ArrayAttr actors) {
  if (version != schedule::metadata::schemaVersion || !sourceGraph ||
      sourceGraph.getWorkers() != 1)
    return emitError() << "invalid static-superstep version or worker count";
  if (!actors || actors.empty())
    return emitError() << "static-superstep actor inventory is absent";
  llvm::SmallDenseSet<Attribute, 16> uniqueActors;
  for (Attribute attribute : actors) {
    auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
    if (!actor || !uniqueActors.insert(actor).second)
      return emitError()
             << "static-superstep actor inventory is invalid or duplicated";
  }
  return success();
}

LogicalResult
FragmentABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t version, DenseI64ArrayAttr fragments) {
  if (version != schedule::metadata::schemaVersion || !fragments)
    return emitError() << "invalid fragment ABI version or inventory";
  llvm::SmallDenseSet<int64_t> ids;
  for (int64_t id : fragments.asArrayRef())
    if (id < 0 || !ids.insert(id).second)
      return emitError() << "fragment ABI has an invalid or duplicate ID";
  return success();
}

LogicalResult
NBASiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                    uint64_t id, uint32_t commit, ComputeNBAStorageKind storage,
                    TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedNBA)
    return emitError() << "NBA timing site must have delayed_nba kind";
  return success();
}

LogicalResult
EventSiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                      uint64_t id, uint32_t commit, TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedEvent)
    return emitError()
           << "deferred-event timing site must have delayed_event kind";
  return success();
}

LogicalResult PeriodicClockAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t actorSlot,
    uint32_t continuation, uint32_t staticState, uint64_t bitOffset,
    uint64_t halfPeriod, ArrayRef<uint64_t> coveragePoints) {
  if (!continuation || !halfPeriod)
    return emitError()
           << "periodic clock requires a continuation and positive half-period";
  return success();
}
LogicalResult
PeriodicAliasAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint32_t sourceStaticState,
                          uint32_t forwardingActorSlot,
                          uint32_t forwardingContinuation,
                          uint32_t targetStaticState, uint64_t sourceBitOffset,
                          uint64_t targetBitOffset, uint64_t driverBitOffset) {
  if (!forwardingContinuation)
    return emitError() << "periodic alias requires a forwarding continuation";
  return success();
}

} // namespace obelisk::schedule
