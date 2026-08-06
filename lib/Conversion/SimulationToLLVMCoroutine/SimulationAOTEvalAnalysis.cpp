//===- SimulationAOTEvalAnalysis.cpp - Resolve generated eval plan -------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/StableHandle.h"

#include "llvm/ADT/STLExtras.h"

#include <numeric>

using namespace mlir;

namespace obelisk::detail {
namespace {

bool rangesOverlap(sim::ComputeEffectAttr lhs, sim::ComputeEffectAttr rhs) {
  if (!lhs || !rhs || lhs.getResource() != rhs.getResource() ||
      lhs.getTarget() != rhs.getTarget() ||
      lhs.getDescriptor() != rhs.getDescriptor() ||
      lhs.getFormal() != rhs.getFormal() || lhs.getWidth() == 0 ||
      rhs.getWidth() == 0)
    return false;
  // ComputeEffectAttr verification guarantees both packed ranges fit in
  // uint64_t. Keep the subtraction form so this analysis does not introduce
  // an independent overflow precondition.
  return lhs.getLow() < rhs.getLow() + rhs.getWidth() &&
         rhs.getLow() < lhs.getLow() + lhs.getWidth();
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
                      sim::ComputeGraphAttr computeGraph,
                      ArrayRef<NativePeriodicClock> periodicClocks,
                      ArrayRef<NativePeriodicAlias> periodicAliases) {
  ResolvedNativeEvalPlan result;
  if (!staticFanoutPlan.exact)
    return result;
  result.fanoutEntries.assign(staticFanoutPlan.entries.begin(),
                              staticFanoutPlan.entries.end());
  for (obelisk_rt_static_fanout_entry &entry : result.fanoutEntries) {
    auto node = llvm::find_if(executableNodes, [&](const auto &candidate) {
      return candidate.actor_slot == entry.actor_slot &&
             candidate.continuation == entry.continuation;
    });
    if (node == executableNodes.end())
      return module.emitError("static fanout has no indexed compute fragment"),
             failure();
    entry.compute_node = static_cast<uint32_t>(node - executableNodes.begin());
    entry.reserved = OBELISK_RT_FANOUT_RUNTIME;
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
  for (auto [index, kernel] : llvm::enumerate(result.clockKernels)) {
    kernel.ingressName = "__obelisk_aot_model_ingress_v1";
    kernel.activeName =
        (Twine("__obelisk_aot_model_active_v1_") + Twine(index)).str();
  }

  if (evalOwnership.fanoutOwners.size() != result.fanoutEntries.size())
    return module.emitError("eval ownership plan does not match fanout"),
           failure();
  for (auto [entryIndex, entry] : llvm::enumerate(result.fanoutEntries)) {
    const auto &executable = executableNodes[entry.compute_node];
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
      entry.reserved = OBELISK_RT_FANOUT_PERIODIC_ALIAS;
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
      entry.reserved = OBELISK_RT_FANOUT_RUNTIME;
      entry.merged_bit = 0;
      continue;
    }
    const NativeDirectFragment *direct =
        plannedOwner.directFragment < directFragments.size()
            ? &directFragments[plannedOwner.directFragment]
            : nullptr;
    if (!direct)
      return module.emitError("generated eval owner references an invalid "
                              "direct fragment"),
             failure();
    auto merged =
        llvm::find_if(result.mergedFragments, [&](const auto &candidate) {
          size_t index =
              static_cast<size_t>(&candidate - result.mergedFragments.data());
          if (direct)
            return index < result.mergedExecutors.size() &&
                   result.mergedExecutors[index] == direct->wrapper &&
                   candidate.actor_slot == direct->actorSlot &&
                   candidate.continuation == direct->continuation;
          return candidate.actor_slot == executable.actor_slot &&
                 candidate.continuation == executable.continuation;
        });
    if (merged != result.mergedFragments.end()) {
      entry.merged_bit = merged->bit;
      size_t mergedIndex =
          static_cast<size_t>(merged - result.mergedFragments.begin());
      entry.reserved = !result.mergedExecutors[mergedIndex].empty()
                           ? OBELISK_RT_FANOUT_DIRECT
                           : OBELISK_RT_FANOUT_RUNTIME;
      merged->compute_node = std::min(merged->compute_node, entry.compute_node);
      continue;
    }

    entry.merged_bit = static_cast<uint32_t>(result.mergedFragments.size());
    entry.reserved =
        direct ? OBELISK_RT_FANOUT_DIRECT : OBELISK_RT_FANOUT_RUNTIME;
    result.mergedFragments.push_back(
        {direct ? direct->actorSlot : executable.actor_slot,
         direct ? direct->continuation : executable.continuation, entry.kernel,
         entry.merged_bit, entry.compute_node, 0, nullptr});
    result.mergedExecutors.push_back(direct ? direct->wrapper : std::string{});
    result.mergedTwoStateExecutors.push_back(direct ? direct->twoStateWrapper
                                                    : std::string{});
    result.mergedPromotionRanges.emplace_back();
    if (direct)
      llvm::append_range(result.mergedPromotionRanges.back(),
                         direct->promotionRanges);
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
      if (entry.reserved == OBELISK_RT_FANOUT_DIRECT)
        entry.merged_bit = remap[entry.merged_bit];
  }

  // Promotion belongs to the exact periodic execution closure. Follow graph
  // activation and matching NBA stage/activate ranges without pulling dormant
  // asynchronous owners into the scan.
  if (!result.mergedFragments.empty() && computeGraph) {
    llvm::SmallDenseSet<unsigned, 32> closure;
    auto clockTouches = [&](const NativePeriodicClock &clock,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != clock.staticState || entry.bit_width == 0)
        return false;
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == clock.staticState;
          });
      if (bound == stateLayout.bounds.end() ||
          clock.bitOffset < bound->offset ||
          clock.bitOffset - bound->offset >= bound->width)
        return false;
      uint64_t bit = clock.bitOffset - bound->offset;
      return entry.low_bit <= bit && bit - entry.low_bit < entry.bit_width;
    };
    auto aliasTouches = [&](const NativePeriodicAlias &alias,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != alias.targetStaticState || entry.bit_width == 0)
        return false;
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == alias.targetStaticState;
          });
      if (bound == stateLayout.bounds.end() ||
          alias.targetBitOffset < bound->offset ||
          alias.targetBitOffset - bound->offset >= bound->width)
        return false;
      uint64_t bit = alias.targetBitOffset - bound->offset;
      return entry.low_bit <= bit && bit - entry.low_bit < entry.bit_width;
    };
    for (const auto &entry : result.fanoutEntries) {
      if (entry.reserved == OBELISK_RT_FANOUT_RUNTIME ||
          entry.reserved == OBELISK_RT_FANOUT_PERIODIC_ALIAS ||
          entry.merged_bit >= result.mergedFragments.size())
        continue;
      bool periodic = llvm::any_of(periodicClocks, [&](const auto &clock) {
        return clockTouches(clock, entry);
      });
      periodic |= llvm::any_of(periodicAliases, [&](const auto &alias) {
        return aliasTouches(alias, entry);
      });
      if (periodic)
        closure.insert(entry.merged_bit);
    }
    const llvm::SmallDenseSet<unsigned, 32> periodicSeeds = closure;
    result.periodicEntryRecords.assign(periodicSeeds.begin(),
                                       periodicSeeds.end());
    llvm::sort(result.periodicEntryRecords);

    llvm::DenseMap<uint32_t, unsigned> fragmentOwners;
    SmallVector<SmallVector<uint32_t>> ownerFragments(
        result.mergedFragments.size());
    std::optional<std::tuple<uint32_t, unsigned, unsigned>> overlap;
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
      SmallVector<uint32_t> ownedFragments;
      llvm::append_range(ownedFragments, direct->fragmentIDs);
      llvm::sort(ownedFragments);
      ownedFragments.erase(
          std::unique(ownedFragments.begin(), ownedFragments.end()),
          ownedFragments.end());
      for (uint32_t fragment : ownedFragments) {
        ownerFragments[recordIndex].push_back(fragment);
        auto [found, inserted] =
            fragmentOwners.try_emplace(fragment, recordIndex);
        if (!inserted && found->second != recordIndex)
          overlap = std::tuple{fragment, found->second, recordIndex};
      }
    }
    if (overlap) {
      auto [fragment, firstOwner, secondOwner] = *overlap;
      return module.emitError("eval fragment ")
             << fragment << " is covered by distinct direct owners "
             << firstOwner << " and " << secondOwner;
    }

    llvm::SmallDenseSet<uint32_t, 64> reachableNodes;
    for (unsigned owner : closure)
      for (uint32_t fragment : ownerFragments[owner])
        reachableNodes.insert(fragment);
    auto reachOwner = [&](uint32_t fragment) {
      auto target = fragmentOwners.find(fragment);
      if (target == fragmentOwners.end() ||
          !closure.insert(target->second).second)
        return false;
      for (uint32_t owned : ownerFragments[target->second])
        reachableNodes.insert(owned);
      return true;
    };
    bool changed;
    do {
      changed = false;
      for (Attribute attribute : computeGraph.getEdges()) {
        auto edge = cast<sim::ComputeEdgeAttr>(attribute);
        if (!reachableNodes.contains(edge.getSource()))
          continue;
        if (edge.getKind() == sim::ComputeEdgeKind::Sensitivity ||
            edge.getKind() == sim::ComputeEdgeKind::Resume) {
          changed |= reachableNodes.insert(edge.getTarget()).second;
          changed |= reachOwner(edge.getTarget());
          continue;
        }
        if (edge.getKind() != sim::ComputeEdgeKind::NBAStage)
          continue;
        for (Attribute candidate : computeGraph.getEdges()) {
          auto activation = cast<sim::ComputeEdgeAttr>(candidate);
          if (activation.getKind() != sim::ComputeEdgeKind::NBAActivate ||
              activation.getSource() != edge.getTarget() ||
              !rangesOverlap(edge.getResource(), activation.getResource()))
            continue;
          changed |= reachableNodes.insert(activation.getTarget()).second;
          changed |= reachOwner(activation.getTarget());
        }
      }
    } while (changed);
    result.periodicClosureRecords.assign(closure.begin(), closure.end());
    llvm::sort(result.periodicClosureRecords);

  }

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
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    auto &mask = fragmentNBARoots[fragment.getId()];
    mask.resize(result.nbaTaintWordCount, 0);
    for (Attribute raw : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(raw);
      if (effect.getEffect() != sim::ComputeEffectKind::NBA)
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
      auto edge = cast<sim::ComputeEdgeAttr>(attribute);
      if (edge.getKind() != sim::ComputeEdgeKind::Sensitivity &&
          edge.getKind() != sim::ComputeEdgeKind::Resume &&
          edge.getKind() != sim::ComputeEdgeKind::Spawn)
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
