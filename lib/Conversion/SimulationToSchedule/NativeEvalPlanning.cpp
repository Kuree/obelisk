//===- SimulationAOTEvalAnalysis.cpp - Resolve generated eval plan -------===//

#include "../SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

#include "obelisk/Runtime/ActivationOrder.h"
#include "obelisk/Runtime/StableHandle.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <numeric>

using namespace mlir;

namespace obelisk::detail {
namespace {

bool rangesOverlap(schedule::ComputeEffectAttr lhs,
                   schedule::ComputeEffectAttr rhs) {
  if (!lhs || !rhs || lhs.getResource() != rhs.getResource() ||
      lhs.getTarget() != rhs.getTarget() ||
      lhs.getDescriptor() != rhs.getDescriptor() ||
      lhs.getFormal() != rhs.getFormal() || lhs.getWidth() == 0 ||
      rhs.getWidth() == 0)
    return false;
  // schedule::ComputeEffectAttr verification guarantees both packed ranges fit
  // in uint64_t. Keep the subtraction form so this analysis does not introduce
  // an independent overflow precondition.
  return lhs.getLow() < rhs.getLow() + rhs.getWidth() &&
         rhs.getLow() < lhs.getLow() + lhs.getWidth();
}

/// Inventory closed zero-time activations using current graph identities.
/// Always_comb and always_latch retain their source predicates and stores;
/// neither keyword proves that every output is assigned on every activation.
/// Reads through dynamic selectors and unrepresented effects remain boundaries.
/// Possible feedback edges are retained, including mux-dependent routes: graph
/// shape never proves a route inactive under the current configuration.
SmallVector<NativeRankedEvalNode>
collectRankedNodes(ModuleOp module, const ResolvedNativeEvalPlan &plan,
                   const NativeStateLayout &stateLayout,
                   const NativeStaticFanoutPlan &fanout,
                   ArrayRef<NativeDirectFragment> directFragments,
                   schedule::ComputeGraphAttr graph) {
  if (!graph)
    return {};
  llvm::StringMap<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    functions.try_emplace(function.getSymName(), function);
  });
  DenseMap<uint32_t, schedule::ComputeFragmentAttr> fragments;
  for (Attribute raw : graph.getNodes())
    if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw))
      fragments.try_emplace(fragment.getId(), fragment);
  llvm::StringMap<const NativeDirectFragment *> directByWrapper;
  for (const auto &direct : directFragments)
    directByWrapper.try_emplace(direct.wrapper, &direct);
  struct Range {
    uint32_t state;
    uint64_t low, high;
  };
  struct Node {
    NativeRankedEvalNode plan;
    SmallVector<Range> reads, writes;
  };
  SmallVector<Node, 0> nodes;
  DenseMap<uint32_t, uint64_t> widths;
  for (const auto &bound : stateLayout.bounds)
    widths.try_emplace(bound.handleID, bound.width);
  APInt subsumed(std::max<size_t>(64, plan.mergedFragments.size()), 0);
  for (const APInt &mask : plan.ownerSubsumptionMasks)
    subsumed |= mask;
  for (auto [owner, record] : llvm::enumerate(plan.mergedFragments)) {
    auto found = directByWrapper.find(plan.mergedExecutors[owner]);
    if (found == directByWrapper.end())
      continue;
    const auto &direct = *found->second;
    if (direct.instanceCoordinator || direct.fragmentIDs.empty() ||
        subsumed[record.bit] || !plan.ownerSubsumptionMasks[owner].isZero())
      continue;
    Node node{{record.bit, direct.body, direct.twoStateBody, {}}, {}, {}};
    bool safe = llvm::all_of(direct.fragmentIDs, [&](uint32_t id) {
      auto fragment = fragments.find(id);
      if (fragment == fragments.end() ||
          fragment->second.getRegion() != schedule::ComputeRegionKind::Active)
        return false;
      auto function =
          functions.lookup(fragment->second.getFunction().getValue());
      if (!function ||
          (function.getEntryKind() != sim::EntryKind::Continuous &&
           function.getEntryKind() != sim::EntryKind::Always &&
           function.getEntryKind() != sim::EntryKind::AlwaysComb &&
           function.getEntryKind() != sim::EntryKind::AlwaysLatch &&
           function.getEntryKind() != sim::EntryKind::PortInput &&
           function.getEntryKind() != sim::EntryKind::PortOutput))
        return false;
      return llvm::all_of(fragment->second.getEffects(), [&](Attribute raw) {
        auto effect = cast<schedule::ComputeEffectAttr>(raw);
        if (effect.getEffect() != schedule::ComputeEffectKind::Read &&
            effect.getEffect() != schedule::ComputeEffectKind::Write &&
            effect.getEffect() != schedule::ComputeEffectKind::Watch)
          return false;
        if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
            effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
            effect.getDynamic() || effect.getDeferred() || !effect.getWidth())
          return false;
        auto handle = stateLayout.storage.find(effect.getDescriptor());
        obelisk_rt_stable_handle_v1 decoded{};
        if (handle == stateLayout.storage.end() ||
            !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
            decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset)
          return false;
        auto width = widths.find(decoded.id);
        if (width == widths.end() || effect.getLow() >= width->second ||
            effect.getWidth() > width->second - effect.getLow())
          return false;
        Range range{decoded.id, effect.getLow(),
                    effect.getLow() + effect.getWidth()};
        if (effect.getEffect() == schedule::ComputeEffectKind::Write) {
          if (fanout.runtimeTransitionStates.contains(decoded.id))
            return false;
          node.writes.push_back(range);
        } else
          node.reads.push_back(range);
        return true;
      });
    });
    // IEEE 1800-2023 9.2.2.2.1 excludes written expressions from always_comb
    // sensitivity. A read-before-write source therefore cannot be treated as
    // an algebraic feedback equation and iterated to a fixed point. Keep
    // self-dependent processes on their existing executor for now.
    for (const auto &write : node.writes)
      for (const auto &read : node.reads)
        safe &= write.state != read.state || write.low >= read.high ||
                read.low >= write.high;
    if (safe && !node.writes.empty())
      nodes.push_back(std::move(node));
  }
  struct Reader {
    uint64_t low, high, prefixHigh;
    uint32_t owner;
  };
  DenseMap<uint32_t, SmallVector<Reader>> readers;
  for (const auto &node : nodes)
    for (const auto &read : node.reads)
      readers[read.state].push_back({read.low, read.high, 0, node.plan.owner});
  for (auto &[state, ranges] : readers) {
    llvm::sort(ranges, [](const Reader &a, const Reader &b) {
      return std::tie(a.low, a.high, a.owner) <
             std::tie(b.low, b.high, b.owner);
    });
    uint64_t high = 0;
    for (auto &range : ranges)
      range.prefixHigh = high = std::max(high, range.high);
  }
  SmallVector<NativeRankedEvalNode> result;
  for (auto &node : nodes) {
    for (const auto &write : node.writes) {
      auto &ranges = readers[write.state];
      auto next = llvm::lower_bound(
          ranges, write.high,
          [](const Reader &r, uint64_t high) { return r.low < high; });
      while (next != ranges.begin()) {
        const auto &read = *--next;
        if (read.prefixHigh <= write.low)
          break;
        if (read.high > write.low)
          node.plan.successors.push_back(read.owner);
      }
    }
    llvm::sort(node.plan.successors);
    node.plan.successors.erase(
        std::unique(node.plan.successors.begin(), node.plan.successors.end()),
        node.plan.successors.end());
    result.push_back(std::move(node.plan));
  }
  DenseMap<uint32_t, uint32_t> dense;
  for (auto [id, node] : llvm::enumerate(result))
    dense.try_emplace(node.owner, id);
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  for (auto [id, node] : llvm::enumerate(result))
    for (uint32_t target : node.successors)
      edges.emplace_back(id, dense.lookup(target));
  // Keep independent cones in separate helper bodies. Interleaving their
  // equally legal topological ranks would sweep unrelated predicates whenever
  // only one cone receives an activation.
  SmallVector<uint32_t> islands(result.size());
  std::iota(islands.begin(), islands.end(), 0);
  auto root = [&](uint32_t node) {
    while (islands[node] != node) {
      islands[node] = islands[islands[node]];
      node = islands[node];
    }
    return node;
  };
  for (auto [from, to] : edges) {
    uint32_t a = root(from), b = root(to);
    islands[std::max(a, b)] = std::min(a, b);
  }
  for (uint32_t id = 0; id < result.size(); ++id)
    result[id].island = root(id);
  runtime::ActivationOrder order;
  if (!runtime::ActivationOrder::build(result.size(), std::move(edges), order))
    return {};
  llvm::stable_sort(order.nodes, [&](uint32_t a, uint32_t b) {
    return result[a].island < result[b].island;
  });
  SmallVector<NativeRankedEvalNode> ranked;
  for (uint32_t id : order.nodes)
    ranked.push_back(std::move(result[id]));
  return ranked;
}

} // namespace

FailureOr<ResolvedNativeEvalPlan>
resolveNativeEvalPlan(ModuleOp module,
                      ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
                      const NativeStateLayout &stateLayout,
                      const NativeStaticNBAPlan &staticNBAPlan,
                      const NativeStaticFanoutPlan &staticFanoutPlan,
                      ArrayRef<NativeDirectFragment> directFragments,
                      const NativeEvalOwnershipPlan &evalOwnership,
                      schedule::ComputeGraphAttr computeGraph,
                      ArrayRef<NativePeriodicClock> periodicClocks,
                      ArrayRef<NativePeriodicAlias> periodicAliases) {
  ResolvedNativeEvalPlan result;
  if (!staticFanoutPlan.exact)
    return result;
  result.fanoutEntries.assign(staticFanoutPlan.entries.begin(),
                              staticFanoutPlan.entries.end());
  auto setFanoutRoute = [](obelisk_rt_static_fanout_entry &entry,
                           uint32_t route) {
    entry.reserved = (entry.reserved & ~OBELISK_RT_FANOUT_ROUTE_MASK) | route;
  };
  for (obelisk_rt_static_fanout_entry &entry : result.fanoutEntries) {
    auto node = llvm::find_if(executableNodes, [&](const auto &candidate) {
      return candidate.actor_slot == entry.actor_slot &&
             candidate.continuation == entry.continuation;
    });
    if (node == executableNodes.end())
      return module.emitError("static fanout has no indexed compute fragment"),
             failure();
    entry.compute_node = static_cast<uint32_t>(node - executableNodes.begin());
    setFanoutRoute(entry, OBELISK_RT_FANOUT_RUNTIME);
    result.clockKernels.push_back(
        {entry.static_state, entry.edge, entry.low_bit, entry.bit_width});
  }
  llvm::sort(result.clockKernels, [](const auto &lhs, const auto &rhs) {
    return lhs.key() < rhs.key();
  });
  result.clockKernels.erase(std::unique(result.clockKernels.begin(),
                                        result.clockKernels.end(),
                                        [](const auto &lhs, const auto &rhs) {
                                          return lhs.key() == rhs.key();
                                        }),
                            result.clockKernels.end());
  for (auto [index, kernel] : llvm::enumerate(result.clockKernels))
    kernel.activeName =
        (Twine("__obelisk_aot_model_active_v1_") + Twine(index)).str();

  if (evalOwnership.fanoutOwners.size() != result.fanoutEntries.size() ||
      evalOwnership.periodicFanoutOwners.size() != result.fanoutEntries.size())
    return module.emitError("eval ownership plan does not match fanout"),
           failure();
  result.periodicOwnerBits.assign(result.fanoutEntries.size(), UINT32_MAX);
  auto resolveOwner =
      [&](const NativeEvalFanoutOwner &plannedOwner,
          const obelisk_rt_static_fanout_entry &entry) -> FailureOr<uint32_t> {
    if (plannedOwner.kind != NativeEvalFanoutOwnerKind::Direct ||
        plannedOwner.directFragment >= directFragments.size())
      return module.emitError(
                 "generated eval owner references an invalid direct fragment"),
             failure();
    const NativeDirectFragment &direct =
        directFragments[plannedOwner.directFragment];
    auto merged =
        llvm::find_if(result.mergedFragments, [&](const auto &candidate) {
          size_t index =
              static_cast<size_t>(&candidate - result.mergedFragments.data());
          return index < result.mergedExecutors.size() &&
                 result.mergedExecutors[index] == direct.wrapper &&
                 candidate.actor_slot == direct.actorSlot &&
                 candidate.continuation == direct.continuation;
        });
    if (merged != result.mergedFragments.end()) {
      merged->compute_node = std::min(merged->compute_node, entry.compute_node);
      return merged->bit;
    }
    uint32_t bit = static_cast<uint32_t>(result.mergedFragments.size());
    result.mergedFragments.push_back({direct.actorSlot, direct.continuation,
                                      entry.kernel, bit, entry.compute_node, 0,
                                      nullptr});
    result.mergedExecutors.push_back(direct.wrapper);
    result.mergedTwoStateExecutors.push_back(direct.twoStateWrapper);
    result.mergedPromotionRanges.emplace_back();
    llvm::append_range(result.mergedPromotionRanges.back(),
                       direct.promotionRanges);
    return bit;
  };
  for (auto [entryIndex, entry] : llvm::enumerate(result.fanoutEntries)) {
    auto trigger = llvm::lower_bound(
        result.clockKernels,
        std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                   static_cast<uint32_t>(entry.edge)},
        [](const NativeEvalClockKernel &kernel, const auto &key) {
          return kernel.key() < key;
        });
    if (trigger == result.clockKernels.end() ||
        trigger->key() != std::tuple{entry.static_state, entry.low_bit,
                                     entry.bit_width,
                                     static_cast<uint32_t>(entry.edge)})
      return module.emitError("could not index generated eval trigger"),
             failure();
    entry.kernel = static_cast<uint32_t>(trigger - result.clockKernels.begin());

    const NativeEvalFanoutOwner &plannedOwner =
        evalOwnership.fanoutOwners[entryIndex];
    if (plannedOwner.kind == NativeEvalFanoutOwnerKind::PeriodicAlias) {
      setFanoutRoute(entry, OBELISK_RT_FANOUT_PERIODIC_ALIAS);
      entry.merged_bit = 0;
      continue;
    }
    // Runtime-owned entries describe transient Tier-3 work (for example a
    // finite reset sequencer) that must be drained before the periodic
    // handoff.  They retain their compute-node identity in the fanout table,
    // but are not members of the closed Tier-1/Tier-2 ready set.  Admitting a
    // null executor here would make the hot coordinator's ownership model
    // incomplete after handoff.
    if (plannedOwner.kind == NativeEvalFanoutOwnerKind::Runtime) {
      setFanoutRoute(entry, OBELISK_RT_FANOUT_RUNTIME);
      entry.merged_bit = 0;
      continue;
    }
    FailureOr<uint32_t> genericBit = resolveOwner(plannedOwner, entry);
    if (failed(genericBit))
      return failure();
    entry.merged_bit = *genericBit;
    setFanoutRoute(entry, OBELISK_RT_FANOUT_DIRECT);

    const NativeEvalFanoutOwner &periodicOwner =
        evalOwnership.periodicFanoutOwners[entryIndex];
    if (periodicOwner.kind != NativeEvalFanoutOwnerKind::Direct)
      continue;
    FailureOr<uint32_t> periodicBit = resolveOwner(periodicOwner, entry);
    if (failed(periodicBit))
      return failure();
    if (*periodicBit != *genericBit)
      result.periodicOwnerBits[entryIndex] = *periodicBit;
  }

  if (!result.mergedFragments.empty()) {
    SmallVector<unsigned> order(result.mergedFragments.size());
    std::iota(order.begin(), order.end(), 0u);
    llvm::sort(order, [&](unsigned lhs, unsigned rhs) {
      const auto &left = result.mergedFragments[lhs];
      const auto &right = result.mergedFragments[rhs];
      return std::tuple{left.compute_node, left.actor_slot, left.continuation} <
             std::tuple{right.compute_node, right.actor_slot,
                        right.continuation};
    });
    SmallVector<uint32_t> remap(result.mergedFragments.size());
    decltype(result.mergedFragments) rankedFragments;
    decltype(result.mergedExecutors) rankedExecutors;
    decltype(result.mergedTwoStateExecutors) rankedTwoStateExecutors;
    decltype(result.mergedPromotionRanges) rankedPromotionRanges;
    for (auto [rank, oldIndex] : llvm::enumerate(order)) {
      remap[oldIndex] = static_cast<uint32_t>(rank);
      auto fragment = result.mergedFragments[oldIndex];
      fragment.bit = static_cast<uint32_t>(rank);
      rankedFragments.push_back(fragment);
      rankedExecutors.push_back(std::move(result.mergedExecutors[oldIndex]));
      rankedTwoStateExecutors.push_back(
          std::move(result.mergedTwoStateExecutors[oldIndex]));
      rankedPromotionRanges.push_back(
          std::move(result.mergedPromotionRanges[oldIndex]));
    }
    result.mergedFragments = std::move(rankedFragments);
    result.mergedExecutors = std::move(rankedExecutors);
    result.mergedTwoStateExecutors = std::move(rankedTwoStateExecutors);
    result.mergedPromotionRanges = std::move(rankedPromotionRanges);
    for (auto &entry : result.fanoutEntries)
      if ((entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK) ==
          OBELISK_RT_FANOUT_DIRECT)
        entry.merged_bit = remap[entry.merged_bit];
    for (uint32_t &bit : result.periodicOwnerBits)
      if (bit != UINT32_MAX)
        bit = remap[bit];
  }

  // Promotion belongs to the exact periodic execution closure. Follow graph
  // activation and matching NBA stage/activate ranges without pulling dormant
  // asynchronous owners into the scan.
  if (!result.mergedFragments.empty() && computeGraph) {
    llvm::SmallDenseSet<unsigned, 32> closure;
    auto periodicBitTouches = [](uint64_t bit,
                                 const obelisk_rt_static_fanout_entry &entry) {
      if (entry.bit_width == 0 || entry.low_bit > bit ||
          bit - entry.low_bit >= entry.bit_width)
        return false;
      // IEEE 1800 edge events on a vector observe only the expression's least
      // significant bit. Ordinary change events observe the complete range.
      return entry.edge == OBELISK_RT_WAIT_EDGE_CHANGE || entry.low_bit == bit;
    };
    auto clockTouches = [&](const NativePeriodicClock &clock,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != clock.getStaticState() || entry.bit_width == 0)
        return false;
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == clock.getStaticState();
          });
      if (bound == stateLayout.bounds.end() ||
          clock.getBitOffset() < bound->offset ||
          clock.getBitOffset() - bound->offset >= bound->width)
        return false;
      uint64_t bit = clock.getBitOffset() - bound->offset;
      return periodicBitTouches(bit, entry);
    };
    auto aliasTouches = [&](const NativePeriodicAlias &alias,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != alias.getTargetStaticState() ||
          entry.bit_width == 0)
        return false;
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == alias.getTargetStaticState();
          });
      if (bound == stateLayout.bounds.end() ||
          alias.getTargetBitOffset() < bound->offset ||
          alias.getTargetBitOffset() - bound->offset >= bound->width)
        return false;
      uint64_t bit = alias.getTargetBitOffset() - bound->offset;
      return periodicBitTouches(bit, entry);
    };
    for (auto [entryIndex, entry] : llvm::enumerate(result.fanoutEntries)) {
      uint32_t route = entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK;
      if (route == OBELISK_RT_FANOUT_RUNTIME ||
          route == OBELISK_RT_FANOUT_PERIODIC_ALIAS ||
          entry.merged_bit >= result.mergedFragments.size())
        continue;
      bool periodic = llvm::any_of(periodicClocks, [&](const auto &clock) {
        return clockTouches(clock, entry);
      });
      periodic |= llvm::any_of(periodicAliases, [&](const auto &alias) {
        return aliasTouches(alias, entry);
      });
      if (periodic) {
        uint32_t owner = result.periodicOwnerBits[entryIndex] == UINT32_MAX
                             ? entry.merged_bit
                             : result.periodicOwnerBits[entryIndex];
        if (owner < result.mergedFragments.size())
          closure.insert(owner);
      }
    }
    const llvm::SmallDenseSet<unsigned, 32> periodicSeeds = closure;
    result.periodicEntryRecords.assign(periodicSeeds.begin(),
                                       periodicSeeds.end());
    llvm::sort(result.periodicEntryRecords);

    SmallVector<SmallVector<uint32_t>> ownerFragments(
        result.mergedFragments.size());
    SmallVector<bool> ownerIsCoordinator(result.mergedFragments.size(), false);
    SmallVector<uint32_t> ownerFusionGroups(result.mergedFragments.size(),
                                            UINT32_MAX);
    for (auto [recordIndex, executor] :
         llvm::enumerate(result.mergedExecutors)) {
      if (executor.empty())
        continue;
      auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
        return candidate.wrapper == executor &&
               candidate.actorSlot ==
                   result.mergedFragments[recordIndex].actor_slot &&
               candidate.continuation ==
                   result.mergedFragments[recordIndex].continuation;
      });
      if (direct == directFragments.end())
        continue;
      ownerIsCoordinator[recordIndex] = direct->instanceCoordinator;
      ownerFusionGroups[recordIndex] = direct->fusionGroup;
      SmallVector<uint32_t> ownedFragments;
      llvm::append_range(ownedFragments, direct->fragmentIDs);
      llvm::sort(ownedFragments);
      ownedFragments.erase(
          std::unique(ownedFragments.begin(), ownedFragments.end()),
          ownedFragments.end());
      ownerFragments[recordIndex] = std::move(ownedFragments);
    }

    result.ownerSubsumptionMasks.assign(
        result.mergedFragments.size(),
        llvm::APInt(std::max<size_t>(64, result.mergedFragments.size()), 0));
    std::optional<std::tuple<uint32_t, unsigned, unsigned>> overlap;
    for (unsigned first = 0; first != result.mergedFragments.size(); ++first) {
      if (ownerFragments[first].empty())
        continue;
      for (unsigned second = first + 1; second != result.mergedFragments.size();
           ++second) {
        if (ownerFragments[second].empty())
          continue;
        auto shared = llvm::find_if(ownerFragments[first], [&](uint32_t id) {
          return llvm::binary_search(ownerFragments[second], id);
        });
        if (shared == ownerFragments[first].end())
          continue;
        unsigned coordinator = ownerIsCoordinator[first] ? first : second;
        unsigned exact = ownerIsCoordinator[first] ? second : first;
        bool certifiedPair =
            ownerIsCoordinator[first] != ownerIsCoordinator[second] &&
            ownerFusionGroups[coordinator] != UINT32_MAX &&
            ownerFusionGroups[coordinator] == ownerFusionGroups[exact];
        bool containsExact =
            certifiedPair &&
            llvm::all_of(ownerFragments[exact], [&](uint32_t fragment) {
              return llvm::binary_search(ownerFragments[coordinator], fragment);
            });
        // The only intentional overlap is a complete Tier-2 body nested in
        // its certified Tier-1 fusion coordinator. Partial overlap is not
        // executable ownership: both wrappers would execute the shared body.
        if (!containsExact) {
          overlap = std::tuple{*shared, first, second};
          break;
        }
        result.ownerSubsumptionMasks[coordinator].setBit(
            result.mergedFragments[exact].bit);
      }
      if (overlap)
        break;
    }
    if (overlap) {
      auto [fragment, firstOwner, secondOwner] = *overlap;
      return module.emitError("eval fragment ")
             << fragment << " is covered by distinct direct owners "
             << firstOwner << " (" << result.mergedExecutors[firstOwner]
             << ") and " << secondOwner << " ("
             << result.mergedExecutors[secondOwner] << ")";
    }

    llvm::DenseMap<uint32_t, unsigned> fragmentOwners;
    for (unsigned owner = 0; owner != result.mergedFragments.size(); ++owner) {
      for (uint32_t fragment : ownerFragments[owner]) {
        auto [found, inserted] = fragmentOwners.try_emplace(fragment, owner);
        if (!inserted && ownerIsCoordinator[owner])
          found->second = owner;
      }
    }

    // Index outgoing dependencies once. An NBA stage reaches only activation
    // edges from its target resource node, and still needs the exact packed
    // range/descriptor overlap proof below (IEEE 1800-2023 4.4, 10.4.2).
    DenseMap<uint32_t, SmallVector<schedule::ComputeEdgeAttr>> outgoingEdges;
    DenseMap<uint32_t, SmallVector<schedule::ComputeEdgeAttr>> nbaActivations;
    for (Attribute attribute : computeGraph.getEdges()) {
      auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
      switch (edge.getKind()) {
      case schedule::ComputeEdgeKind::Sensitivity:
      case schedule::ComputeEdgeKind::Resume:
      case schedule::ComputeEdgeKind::NBAStage:
        outgoingEdges[edge.getSource()].push_back(edge);
        break;
      case schedule::ComputeEdgeKind::NBAActivate:
        nbaActivations[edge.getSource()].push_back(edge);
        break;
      default:
        break;
      }
    }
    llvm::SmallDenseSet<uint32_t, 64> reachableNodes;
    SmallVector<uint32_t> pendingNodes;
    auto reachNode = [&](uint32_t node) {
      if (reachableNodes.insert(node).second)
        pendingNodes.push_back(node);
    };
    for (unsigned owner : closure)
      for (uint32_t fragment : ownerFragments[owner])
        reachNode(fragment);
    auto reachOwner = [&](uint32_t fragment) {
      auto target = fragmentOwners.find(fragment);
      if (target == fragmentOwners.end() ||
          !closure.insert(target->second).second)
        return;
      for (uint32_t owned : ownerFragments[target->second])
        reachNode(owned);
    };
    // Each newly reachable fragment is visited once, including other fragments
    // admitted through its owner. Cycles and duplicate edges cannot repeatedly
    // enqueue nodes; reaching an owner is independent of reaching its node.
    while (!pendingNodes.empty()) {
      auto outgoing = outgoingEdges.find(pendingNodes.pop_back_val());
      if (outgoing == outgoingEdges.end())
        continue;
      for (schedule::ComputeEdgeAttr edge : outgoing->second) {
        if (edge.getKind() != schedule::ComputeEdgeKind::NBAStage) {
          reachNode(edge.getTarget());
          reachOwner(edge.getTarget());
          continue;
        }
        auto activations = nbaActivations.find(edge.getTarget());
        if (activations == nbaActivations.end())
          continue;
        for (schedule::ComputeEdgeAttr activation : activations->second) {
          if (!rangesOverlap(edge.getResource(), activation.getResource()))
            continue;
          reachNode(activation.getTarget());
          reachOwner(activation.getTarget());
        }
      }
    }
    // Tier-1 execution contains these exact Tier-2 bodies, but generated
    // transitions still publish their exact identities.  Keep them in the
    // periodic promotion/transition closure while leaving only the complete
    // coordinator in the periodic entry set.
    SmallVector<unsigned> closureSnapshot(closure.begin(), closure.end());
    for (unsigned owner : closureSnapshot) {
      if (owner >= result.ownerSubsumptionMasks.size())
        continue;
      llvm::APInt members = result.ownerSubsumptionMasks[owner];
      while (!members.isZero()) {
        unsigned bit = members.countr_zero();
        closure.insert(bit);
        members.clearBit(bit);
      }
    }
    result.periodicClosureRecords.assign(closure.begin(), closure.end());
    llvm::sort(result.periodicClosureRecords);
  }

  if (result.ownerSubsumptionMasks.empty())
    result.ownerSubsumptionMasks.assign(
        result.mergedFragments.size(),
        llvm::APInt(std::max<size_t>(64, result.mergedFragments.size()), 0));

  result.rankedNodes =
      collectRankedNodes(module, result, stateLayout, staticFanoutPlan,
                         directFragments, computeGraph);

  // Project graph-level NBA reachability onto exclusive generated owners.
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  result.nbaTaintWordCount = static_cast<uint32_t>((nbaRoots.size() + 63) / 64);
  result.recordNBATaintMasks.assign(
      result.mergedFragments.size(),
      SmallVector<uint64_t>(result.nbaTaintWordCount, 0));
  result.nbaTaintedRecords.resize(result.mergedFragments.size());
  if (result.mergedFragments.empty())
    return result;

  auto fillAllRoots = [&](MutableArrayRef<uint64_t> mask) {
    llvm::fill(mask, UINT64_MAX);
    if (!mask.empty() && (nbaRoots.size() & 63) != 0)
      mask.back() = (uint64_t{1} << (nbaRoots.size() & 63)) - 1;
  };
  if (!computeGraph) {
    for (unsigned index = 0; index != result.mergedFragments.size(); ++index) {
      fillAllRoots(result.recordNBATaintMasks[index]);
      result.nbaTaintedRecords.set(index);
    }
    return result;
  }

  llvm::DenseMap<uint32_t, SmallVector<uint64_t>> fragmentNBARoots;
  for (Attribute attribute : computeGraph.getNodes()) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    auto &mask = fragmentNBARoots[fragment.getId()];
    mask.resize(result.nbaTaintWordCount, 0);
    for (Attribute raw : fragment.getEffects()) {
      auto effect = cast<schedule::ComputeEffectAttr>(raw);
      if (effect.getEffect() != schedule::ComputeEffectKind::NBA)
        continue;
      auto handle = stateLayout.storage.find(effect.getDescriptor());
      obelisk_rt_stable_handle_v1 decoded{};
      if (handle == stateLayout.storage.end() ||
          !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC) {
        fillAllRoots(mask);
        continue;
      }
      auto root = llvm::find_if(nbaRoots, [&](const auto &candidate) {
        return candidate.static_state == decoded.id;
      });
      if (root == nbaRoots.end()) {
        fillAllRoots(mask);
        continue;
      }
      size_t rootIndex = static_cast<size_t>(root - nbaRoots.begin());
      mask[rootIndex / 64] |= uint64_t{1} << (rootIndex % 64);
    }
  }
  bool changed;
  do {
    changed = false;
    for (Attribute attribute : computeGraph.getEdges()) {
      auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
      if (edge.getKind() != schedule::ComputeEdgeKind::Sensitivity &&
          edge.getKind() != schedule::ComputeEdgeKind::Resume &&
          edge.getKind() != schedule::ComputeEdgeKind::Spawn)
        continue;
      auto target = fragmentNBARoots.find(edge.getTarget());
      if (target == fragmentNBARoots.end())
        continue;
      auto &source = fragmentNBARoots[edge.getSource()];
      source.resize(result.nbaTaintWordCount, 0);
      for (uint32_t word = 0; word != result.nbaTaintWordCount; ++word) {
        uint64_t merged = source[word] | target->second[word];
        changed |= merged != source[word];
        source[word] = merged;
      }
    }
  } while (changed);
  for (auto [recordIndex, executor] : llvm::enumerate(result.mergedExecutors)) {
    auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
      return candidate.wrapper == executor &&
             candidate.actorSlot ==
                 result.mergedFragments[recordIndex].actor_slot &&
             candidate.continuation ==
                 result.mergedFragments[recordIndex].continuation;
    });
    if (direct == directFragments.end()) {
      fillAllRoots(result.recordNBATaintMasks[recordIndex]);
      result.nbaTaintedRecords.set(recordIndex);
      continue;
    }
    for (uint32_t fragment : direct->fragmentIDs)
      if (auto roots = fragmentNBARoots.find(fragment);
          roots != fragmentNBARoots.end())
        for (uint32_t word = 0; word != result.nbaTaintWordCount; ++word)
          result.recordNBATaintMasks[recordIndex][word] |= roots->second[word];
    if (llvm::any_of(result.recordNBATaintMasks[recordIndex],
                     [](uint64_t word) { return word != 0; }))
      result.nbaTaintedRecords.set(recordIndex);
  }
  return result;
}

} // namespace obelisk::detail
