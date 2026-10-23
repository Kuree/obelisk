//===- SimulationAOTEvalAnalysis.cpp - Resolve generated eval plan -------===//

#include "../SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Analysis/ActivationRegionAnalysis.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Runtime/ActivationOrder.h"
#include "obelisk/Runtime/StableHandle.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <algorithm>
#include <numeric>

using namespace mlir;

namespace obelisk::detail {
namespace {

/// Change ready-bit identities before any executable tables or NBA taint masks
/// are emitted. Every owner-indexed fact must follow the same permutation.
void reorderOwners(ResolvedNativeEvalPlan &plan, ArrayRef<uint32_t> order) {
  assert(order.size() == plan.mergedFragments.size());
  assert(plan.recordNBATaintMasks.empty() && plan.nbaTaintedRecords.empty());
  SmallVector<uint32_t> remap(order.size());
  for (auto [rank, oldIndex] : llvm::enumerate(order))
    remap[oldIndex] = rank;
  auto reorder = [&](auto &values) {
    if (values.empty())
      return;
    assert(values.size() == order.size());
    auto old = std::move(values);
    values.clear();
    values.reserve(order.size());
    for (uint32_t index : order)
      values.push_back(std::move(old[index]));
  };
  reorder(plan.mergedFragments);
  reorder(plan.mergedExecutors);
  reorder(plan.mergedTwoStateExecutors);
  reorder(plan.mergedPromotionRanges);
  reorder(plan.ownerSubsumptionMasks);
  reorder(plan.deferredOwners);
  for (auto [rank, fragment] : llvm::enumerate(plan.mergedFragments))
    fragment.bit = rank;
  for (auto &mask : plan.ownerSubsumptionMasks) {
    APInt mapped(mask.getBitWidth(), 0);
    while (!mask.isZero()) {
      unsigned bit = mask.countr_zero();
      mapped.setBit(remap[bit]);
      mask.clearBit(bit);
    }
    mask = std::move(mapped);
  }
  for (auto &entry : plan.fanoutEntries)
    if ((entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK) ==
        OBELISK_RT_FANOUT_DIRECT)
      entry.merged_bit = remap[entry.merged_bit];
  for (uint32_t &bit : plan.periodicOwnerBits)
    if (bit != UINT32_MAX)
      bit = remap[bit];
  for (auto *records :
       {&plan.periodicClosureRecords, &plan.periodicEntryRecords}) {
    for (unsigned &record : *records)
      record = remap[record];
    llvm::sort(*records);
  }
  for (auto &node : plan.rankedNodes) {
    node.owner = remap[node.owner];
    for (uint32_t &successor : node.successors)
      successor = remap[successor];
    llvm::sort(node.successors);
  }
}

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
                   schedule::ComputeGraphAttr graph,
                   const NativeActivationInputs &activationInputs) {
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
  auto normalizeRanges = [](SmallVectorImpl<Range> &ranges) {
    llvm::sort(ranges, [](const Range &a, const Range &b) {
      return std::tie(a.state, a.low, a.high) <
             std::tie(b.state, b.low, b.high);
    });
    size_t kept = 0;
    for (Range range : ranges) {
      if (kept && ranges[kept - 1].state == range.state &&
          ranges[kept - 1].high >= range.low)
        ranges[kept - 1].high = std::max(ranges[kept - 1].high, range.high);
      else
        ranges[kept++] = range;
    }
    ranges.resize(kept);
  };
  auto overlap = [](ArrayRef<Range> reads, ArrayRef<Range> writes) {
    size_t r = 0, w = 0;
    while (r < reads.size() && w < writes.size()) {
      const auto &read = reads[r];
      const auto &write = writes[w];
      if (read.state < write.state ||
          (read.state == write.state && read.high <= write.low))
        ++r;
      else if (write.state < read.state || write.high <= read.low)
        ++w;
      else
        return true;
    }
    return false;
  };
  struct Node {
    NativeRankedEvalNode plan;
    SmallVector<Range> reads, writes;
    bool opaqueMemory = false;
  };
  SmallVector<Node, 0> nodes;
  using ClockKey = std::tuple<uint32_t, uint32_t, uint64_t, uint64_t>;
  SmallVector<std::optional<ClockKey>> clocks(plan.mergedFragments.size());
  DenseSet<uint32_t> multipleClocks;
  for (const auto &entry : plan.fanoutEntries) {
    if (entry.merged_bit >= clocks.size() ||
        (entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK) !=
            OBELISK_RT_FANOUT_DIRECT)
      continue;
    ClockKey key{entry.static_state, entry.edge, entry.low_bit,
                 entry.bit_width};
    auto &clock = clocks[entry.merged_bit];
    if ((clock && *clock != key) ||
        (entry.edge != OBELISK_RT_WAIT_EDGE_POSEDGE &&
         entry.edge != OBELISK_RT_WAIT_EDGE_NEGEDGE))
      multipleClocks.insert(entry.merged_bit);
    clock = key;
  }
  SmallVector<std::pair<NativeRankedEvalNode, ClockKey>> clockedNodes;
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
    // A ranked sweep selects a member's two-state body by its promotion
    // certificate alone. An uncertified path-guarded body has no certificate:
    // only its activation-local probe proves the bits it reads known, and a
    // never-written variable still holds X (IEEE 1800-2023 6.8, Table 6-7).
    // Run such a member's four-state body in the sweep.
    StringRef twoStateBody = direct.twoStateBody;
    if (sim::SimFuncOp variant = functions.lookup(twoStateBody);
        variant &&
        ::obelisk::schedule::has<schedule::metadata::evalPathGuardedTwoState>(
            variant) &&
        !::obelisk::schedule::has<
            schedule::metadata::evalPathGuardedKnownPreserving>(variant))
      twoStateBody = {};
    Node node{{record.bit, direct.body, twoStateBody.str(), {}}, {}, {}};
    bool clocked = llvm::any_of(direct.fragmentIDs, [&](uint32_t id) {
      auto fragment = fragments.lookup(id);
      return fragment && llvm::any_of(fragment.getEffects(), [](Attribute raw) {
               return cast<schedule::ComputeEffectAttr>(raw).getEffect() ==
                      schedule::ComputeEffectKind::NBA;
             });
    });
    if (clocked && (!clocks[record.bit] || multipleClocks.contains(record.bit)))
      continue;
    // An owner that resumes in the Inactive region must not join a sweep:
    // it runs only after every Active owner of its pass. It still orders its
    // producers and consumers as a boundary.
    bool deferred =
        owner < plan.deferredOwners.size() && plan.deferredOwners[owner];
    clocked &= !deferred;
    DenseSet<StringAttr> summarizedActivations;
    bool safe = !deferred && llvm::all_of(direct.fragmentIDs, [&](uint32_t id) {
      auto fragment = fragments.find(id);
      if (fragment == fragments.end() ||
          fragment->second.getRegion() != schedule::ComputeRegionKind::Active)
        return false;
      auto function =
          functions.lookup(fragment->second.getFunction().getValue());
      if (!function)
        return false;
      auto kind = function.getEntryKind();
      if (clocked ? kind != sim::EntryKind::Always &&
                        kind != sim::EntryKind::AlwaysFF
                  : kind != sim::EntryKind::Continuous &&
                        kind != sim::EntryKind::Always &&
                        kind != sim::EntryKind::AlwaysComb &&
                        kind != sim::EntryKind::AlwaysLatch &&
                        kind != sim::EntryKind::PortInput &&
                        kind != sim::EntryKind::PortOutput)
        return false;
      // A closed activation may read its own blocking temporaries after
      // defining them. Whole-function may-read effects do not distinguish
      // those internal edges from feedback through the activation boundary.
      // Reuse the finite initial/resume proof and dense must-definition facts
      // before dropping such edges or admitting bounded dynamic accesses.
      // Sensitivity remains independent: every watch stays an ordering edge.
      auto effects = fragment->second.getEffects();
      auto inputs = activationInputs.liveIns.find(function.getSymNameAttr());
      auto outputs = activationInputs.mayWrites.find(function.getSymNameAttr());
      bool activation = !clocked && activationInputs.sourceGraph == graph &&
                        inputs != activationInputs.liveIns.end() &&
                        outputs != activationInputs.mayWrites.end();
      bool effectsSafe = llvm::all_of(effects, [&](Attribute raw) {
        auto effect = cast<schedule::ComputeEffectAttr>(raw);
        // IEEE 1800-2023 4.5-4.7, 10.4.2: consecutive Active evaluations
        // may stage their NBAs in this order, but cannot commit them early.
        // Immediate stores need the separate trigger/observer proof below.
        // Bounded indexed reads/NBAs keep their existing checked lowering
        // and exact publication at the NBA barrier. Delayed NBAs and opaque
        // effects are excluded; emission also checks for runtime boundaries.
        if (effect.getEffect() != schedule::ComputeEffectKind::Read &&
            effect.getEffect() != schedule::ComputeEffectKind::Write &&
            effect.getEffect() != schedule::ComputeEffectKind::Watch &&
            !(clocked &&
              effect.getEffect() == schedule::ComputeEffectKind::NBA))
          return false;
        if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
            effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
            (!clocked && effect.getDynamic() &&
             (!activation ||
              effect.getEffect() == schedule::ComputeEffectKind::Watch)) ||
            effect.getDeferred() || !effect.getWidth())
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
        if (clocked &&
            effect.getEffect() == schedule::ComputeEffectKind::Watch) {
          uint32_t edge =
              effect.getTrigger() == schedule::ComputeTriggerKind::Posedge
                  ? OBELISK_RT_WAIT_EDGE_POSEDGE
              : effect.getTrigger() == schedule::ComputeTriggerKind::Negedge
                  ? OBELISK_RT_WAIT_EDGE_NEGEDGE
                  : OBELISK_RT_WAIT_EDGE_CHANGE;
          if (effect.getDynamic() ||
              ClockKey{decoded.id, edge, effect.getLow(), effect.getWidth()} !=
                  *clocks[record.bit])
            return false;
        }
        Range range{decoded.id, effect.getLow(),
                    effect.getLow() + effect.getWidth()};
        if (effect.getEffect() == schedule::ComputeEffectKind::Write) {
          if (fanout.runtimeTransitionStates.contains(decoded.id))
            return false;
          if (clocked) {
            auto [state, edge, low, width] = *clocks[record.bit];
            // The source-order body and its per-member ready bit
            // preserve intermediate publications. A body must not change
            // its own trigger while it is inactive (LRM 9.4.2); the effect
            // range includes every possible dynamic target, not just the
            // currently selected element.
            if (decoded.id == state && range.low < low + width &&
                low < range.high)
              return false;
          } else if (!activation)
            node.writes.push_back(range);
        } else if (!clocked &&
                   (!activation ||
                    effect.getEffect() == schedule::ComputeEffectKind::Watch))
          node.reads.push_back(range);
        return true;
      });
      if (!effectsSafe)
        return false;
      if (activation &&
          summarizedActivations.insert(function.getSymNameAttr()).second) {
        auto appendRanges = [&](auto ranges, SmallVectorImpl<Range> &dest) {
          for (auto range : ranges) {
            constexpr uint64_t netTag =
                analysis::ActivationMemoryAnalysis::netRootTag;
            const auto &handles =
                range.root & netTag ? stateLayout.nets : stateLayout.storage;
            auto handle = handles.find(range.root & ~netTag);
            obelisk_rt_stable_handle_v1 decoded{};
            if (handle == handles.end() ||
                !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
                decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
                decoded.offset)
              return false;
            auto width = widths.find(decoded.id);
            if (width == widths.end() || range.low >= width->second ||
                range.width > width->second - range.low)
              return false;
            dest.push_back({decoded.id, range.low, range.low + range.width});
          }
          return true;
        };
        return appendRanges(inputs->second, node.reads) &&
               appendRanges(outputs->second, node.writes);
      }
      return true;
    });
    // IEEE 1800-2023 9.2.2.2.1 excludes written expressions from always_comb
    // sensitivity. A read-before-write source therefore cannot be treated as
    // an algebraic feedback equation and iterated to a fixed point. Keep
    // self-dependent processes on their existing executor for now.
    if (safe) {
      normalizeRanges(node.reads);
      normalizeRanges(node.writes);
      safe = !overlap(node.reads, node.writes);
    }
    if (safe && clocked) {
      node.plan.clocked = true;
      clockedNodes.emplace_back(std::move(node.plan), *clocks[record.bit]);
    } else if (safe && !node.writes.empty())
      nodes.push_back(std::move(node));
    else if (!clocked) {
      // Scheduling only the certified helper subgraph can jump over a live
      // producer that still uses its original executor. Include such owners
      // in the dependence graph as boundaries, with their conservative full
      // effect inventory. Group emission must stop at each boundary.
      node.plan.boundary = true;
      node.reads.clear();
      node.writes.clear();
      DenseSet<StringAttr> summarizedOwners;
      for (uint32_t id : direct.fragmentIDs) {
        auto fragment = fragments.lookup(id);
        if (!fragment) {
          node.opaqueMemory = true;
          continue;
        }
        auto owner = fragment.getFunction().getAttr();
        auto inputs = activationInputs.liveIns.find(owner);
        auto outputs = activationInputs.mayWrites.find(owner);
        bool activation = activationInputs.sourceGraph == graph &&
                          inputs != activationInputs.liveIns.end() &&
                          outputs != activationInputs.mayWrites.end();
        if (activation && summarizedOwners.insert(owner).second) {
          auto appendRanges = [&](auto ranges, SmallVectorImpl<Range> &dest) {
            for (auto range : ranges) {
              constexpr uint64_t netTag =
                  analysis::ActivationMemoryAnalysis::netRootTag;
              const auto &handles =
                  range.root & netTag ? stateLayout.nets : stateLayout.storage;
              auto handle = handles.find(range.root & ~netTag);
              obelisk_rt_stable_handle_v1 decoded{};
              if (handle == handles.end() ||
                  !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
                  decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
                  decoded.offset) {
                node.opaqueMemory = true;
                continue;
              }
              auto width = widths.find(decoded.id);
              if (width == widths.end() || range.low >= width->second ||
                  range.width > width->second - range.low) {
                node.opaqueMemory = true;
                continue;
              }
              dest.push_back({decoded.id, range.low, range.low + range.width});
            }
          };
          appendRanges(inputs->second, node.reads);
          appendRanges(outputs->second, node.writes);
        }
        for (auto effect :
             fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
          auto kind = effect.getEffect();
          if (kind != schedule::ComputeEffectKind::Read &&
              kind != schedule::ComputeEffectKind::Write &&
              kind != schedule::ComputeEffectKind::Drive &&
              kind != schedule::ComputeEffectKind::Watch)
            continue;
          // A certified activation accounts for every canonical access and
          // excludes its private scratch storage. Keep that precision even
          // when net publication or another emission restriction requires an
          // executor boundary. Unknown local effects must not invent edges to
          // every other actor. Sensitivity remains an independent constraint.
          if (activation && kind != schedule::ComputeEffectKind::Watch)
            continue;
          auto resource = effect.getResource();
          if ((resource != schedule::ComputeResourceKind::Storage &&
               resource != schedule::ComputeResourceKind::Net) ||
              effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
              !effect.getWidth()) {
            node.opaqueMemory = true;
            continue;
          }
          // The whole-program effect solver already maps driver publications
          // to their net and expands collapsed port aliases. A known net is
          // a distinct address space, not an unknown write to all storage.
          // Keep its executor as a boundary: ordering does not bypass net
          // resolution, strengths, delay handling, or transition observers.
          const auto &handles = resource == schedule::ComputeResourceKind::Net
                                    ? stateLayout.nets
                                    : stateLayout.storage;
          auto handle = handles.find(effect.getDescriptor());
          obelisk_rt_stable_handle_v1 decoded{};
          if (handle == handles.end() ||
              !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
              decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
              decoded.offset) {
            node.opaqueMemory = true;
            continue;
          }
          auto width = widths.find(decoded.id);
          if (width == widths.end() || effect.getLow() >= width->second ||
              effect.getWidth() > width->second - effect.getLow()) {
            node.opaqueMemory = true;
            continue;
          }
          Range range{decoded.id, effect.getLow(),
                      effect.getLow() + effect.getWidth()};
          (kind == schedule::ComputeEffectKind::Write ||
                   kind == schedule::ComputeEffectKind::Drive
               ? node.writes
               : node.reads)
              .push_back(range);
        }
      }
      normalizeRanges(node.reads);
      normalizeRanges(node.writes);
      nodes.push_back(std::move(node));
    }
  }
  struct Reader {
    uint64_t low, high, prefixHigh;
    uint32_t owner;
  };
  if (native::has<native::DebugTiming>(module))
    for (const auto &node : nodes)
      llvm::errs() << "obelisk ranked memory: body=" << node.plan.body
                   << " reads=" << node.reads.size()
                   << " writes=" << node.writes.size() << '\n';
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
  SmallVector<uint32_t> opaqueOwners;
  for (const auto &node : nodes)
    if (node.opaqueMemory)
      opaqueOwners.push_back(node.plan.owner);
  SmallVector<NativeRankedEvalNode> result;
  for (auto &node : nodes) {
    if (node.opaqueMemory) {
      for (const auto &other : nodes)
        if (node.plan.owner != other.plan.owner)
          node.plan.successors.push_back(other.plan.owner);
    } else
      llvm::append_range(node.plan.successors, opaqueOwners);
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
  // Each exact physical edge gets a fixed-order direct-call sequence, using
  // the existing ready/proof/NBA-taint protocol. Independent clocks and async
  // resets stay separate. A checkpoint can enqueue a subset, so emission still
  // checks every member's ready bit instead of inventing co-activations.
  DenseMap<ClockKey, uint32_t> cohorts;
  uint32_t nextIsland = result.size();
  for (auto &[node, clock] : clockedNodes) {
    auto [cohort, inserted] = cohorts.try_emplace(clock, nextIsland);
    nextIsland += inserted;
    node.island = cohort->second;
    ranked.push_back(std::move(node));
  }
  return ranked;
}

} // namespace

NativeActivationInputs collectNativeActivationInputs(sim::SimDesignOp design,
                                                     bool explain) {
  NativeActivationInputs result;
  if (!design || !(result.sourceGraph = design.getComputeGraphAttr()))
    return result;
  analysis::HandleDataflowAnalysis handles(design);
  result.completeNBAWriters = true;
  SymbolTable symbols(design);
  llvm::MapVector<StringAttr, SmallVector<schedule::ComputeEffectAttr>>
      effectsByOwner;
  DenseSet<StringAttr> changeOwners;
  for (auto raw : result.sourceGraph.getNodes()) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
    if (!fragment)
      continue;
    for (auto effect :
         fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
      if (effect.getEffect() != schedule::ComputeEffectKind::NBA)
        continue;
      if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
          effect.getTarget() != schedule::ComputeTargetKind::Descriptor) {
        result.completeNBAWriters = false;
        continue;
      }
      result.nbaWriters[effect.getDescriptor()].push_back(fragment.getId());
    }
    StringAttr owner = fragment.getFunction().getAttr();
    if (fragment.getAction() == schedule::ComputeActionKind::SuspendChange ||
        fragment.getAction() == schedule::ComputeActionKind::SuspendAny)
      changeOwners.insert(owner);
    llvm::append_range(
        effectsByOwner[owner],
        fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>());
  }
  for (const auto &[owner, effects] : effectsByOwner) {
    if (!changeOwners.contains(owner))
      continue;
    bool needsProof = llvm::any_of(effects, [&](auto effect) {
      if (effect.getDynamic())
        return true;
      if (effect.getEffect() != schedule::ComputeEffectKind::Read)
        return false;
      return llvm::any_of(effects, [&](auto write) {
        return write.getEffect() == schedule::ComputeEffectKind::Write &&
               rangesOverlap(effect, write);
      });
    });
    if (!needsProof)
      continue;
    auto function = symbols.lookup<sim::SimFuncOp>(owner.getValue());
    if (!function)
      continue;
    analysis::ActivationRegionAnalysis activation(
        function, handles, {},
        analysis::ActivationMemoryAnalysis::DependencyMode::Inputs);
    if (explain)
      llvm::errs()
          << "obelisk ranked activation: " << function.getSymName()
          << " valid=" << activation.isValid()
          << " complete=" << activation.getMemory().hasCompleteWrites()
          << " inputs=" << activation.getMemory().hasInputDependencies()
          << " input_reason=" << activation.getMemory().getDependencyFailure()
          << " reason="
          << analysis::stringifyActivationFailure(activation.getFailureReason())
          << '\n';
    if (activation.isValid() && activation.getMemory().hasCompleteWrites()) {
      llvm::append_range(result.liveIns[function.getSymNameAttr()],
                         activation.getMemory().hasInputDependencies()
                             ? activation.getMemory().getValueInputs()
                             : activation.getMemory().getLiveIns());
      llvm::append_range(result.mayWrites[function.getSymNameAttr()],
                         activation.getMemory().getMayWrites());
    }
  }
  return result;
}

FailureOr<ResolvedNativeEvalPlan>
resolveNativeEvalPlan(ModuleOp module,
                      ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
                      const NativeStateLayout &stateLayout,
                      const NativeStaticNBAPlan &staticNBAPlan,
                      const NativeStaticFanoutPlan &staticFanoutPlan,
                      ArrayRef<NativeDirectFragment> directFragments,
                      const NativeEvalOwnershipPlan &evalOwnership,
                      schedule::ComputeGraphAttr computeGraph,
                      const NativeActivationInputs &activationInputs,
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
  // The first executable node of each physical continuation.
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> nodeByContinuation;
  for (auto [index, node] : llvm::enumerate(executableNodes))
    nodeByContinuation.try_emplace({node.actor_slot, node.continuation},
                                   static_cast<uint32_t>(index));
  for (obelisk_rt_static_fanout_entry &entry : result.fanoutEntries) {
    auto node = nodeByContinuation.find({entry.actor_slot, entry.continuation});
    if (node == nodeByContinuation.end())
      return module.emitError("static fanout has no indexed compute fragment"),
             failure();
    entry.compute_node = node->second;
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
  // Each executor's merged records, in record order.
  llvm::StringMap<SmallVector<uint32_t, 1>> mergedByExecutor;
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
    SmallVector<uint32_t, 1> &records = mergedByExecutor[direct.wrapper];
    auto merged = llvm::find_if(records, [&](uint32_t index) {
      return result.mergedFragments[index].actor_slot == direct.actorSlot &&
             result.mergedFragments[index].continuation == direct.continuation;
    });
    if (merged != records.end()) {
      auto &record = result.mergedFragments[*merged];
      record.compute_node = std::min(record.compute_node, entry.compute_node);
      return record.bit;
    }
    uint32_t bit = static_cast<uint32_t>(result.mergedFragments.size());
    records.push_back(bit);
    result.mergedFragments.push_back({direct.actorSlot, direct.continuation,
                                      entry.kernel, bit, entry.compute_node, 0,
                                      nullptr});
    result.deferredOwners.push_back(direct.deferred);
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
    SmallVector<uint32_t> order(result.mergedFragments.size());
    std::iota(order.begin(), order.end(), 0u);
    llvm::sort(order, [&](unsigned lhs, unsigned rhs) {
      const auto &left = result.mergedFragments[lhs];
      const auto &right = result.mergedFragments[rhs];
      return std::tuple{left.compute_node, left.actor_slot, left.continuation} <
             std::tuple{right.compute_node, right.actor_slot,
                        right.continuation};
    });
    reorderOwners(result, order);
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
    // The state-local bit of a periodic source, if it lies in its state.
    auto localBit = [&](uint64_t state,
                        uint64_t offset) -> std::optional<uint64_t> {
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == state;
          });
      if (bound == stateLayout.bounds.end() || offset < bound->offset ||
          offset - bound->offset >= bound->width)
        return std::nullopt;
      return offset - bound->offset;
    };
    SmallVector<std::optional<uint64_t>> clockBits, aliasBits;
    for (const NativePeriodicClock &clock : periodicClocks)
      clockBits.push_back(
          localBit(clock.getStaticState(), clock.getBitOffset()));
    for (const NativePeriodicAlias &alias : periodicAliases)
      aliasBits.push_back(
          localBit(alias.getTargetStaticState(), alias.getTargetBitOffset()));
    auto clockTouches = [&](size_t index,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != periodicClocks[index].getStaticState() ||
          entry.bit_width == 0 || !clockBits[index])
        return false;
      return periodicBitTouches(*clockBits[index], entry);
    };
    auto aliasTouches = [&](size_t index,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != periodicAliases[index].getTargetStaticState() ||
          entry.bit_width == 0 || !aliasBits[index])
        return false;
      return periodicBitTouches(*aliasBits[index], entry);
    };
    for (auto [entryIndex, entry] : llvm::enumerate(result.fanoutEntries)) {
      uint32_t route = entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK;
      if (route == OBELISK_RT_FANOUT_RUNTIME ||
          route == OBELISK_RT_FANOUT_PERIODIC_ALIAS ||
          entry.merged_bit >= result.mergedFragments.size())
        continue;
      bool periodic = false;
      for (size_t index = 0; index != periodicClocks.size() && !periodic;
           ++index)
        periodic = clockTouches(index, entry);
      for (size_t index = 0; index != periodicAliases.size() && !periodic;
           ++index)
        periodic = aliasTouches(index, entry);
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
    llvm::StringMap<SmallVector<unsigned, 1>> directsByWrapper;
    for (auto [index, direct] : llvm::enumerate(directFragments))
      directsByWrapper[direct.wrapper].push_back(index);
    for (auto [recordIndex, executor] :
         llvm::enumerate(result.mergedExecutors)) {
      if (executor.empty())
        continue;
      auto candidates = directsByWrapper.find(executor);
      if (candidates == directsByWrapper.end())
        continue;
      auto found = llvm::find_if(candidates->second, [&](unsigned candidate) {
        return directFragments[candidate].actorSlot ==
                   result.mergedFragments[recordIndex].actor_slot &&
               directFragments[candidate].continuation ==
                   result.mergedFragments[recordIndex].continuation;
      });
      if (found == candidates->second.end())
        continue;
      const NativeDirectFragment *direct = &directFragments[*found];
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
    // Only owner pairs that share a fragment can overlap. Visit exactly those
    // pairs, in the (first, second) order of an all-pairs scan.
    DenseMap<uint32_t, SmallVector<unsigned, 2>> fragmentOwnerLists;
    for (auto [owner, fragments] : llvm::enumerate(ownerFragments))
      for (uint32_t fragment : fragments)
        fragmentOwnerLists[fragment].push_back(owner);
    SmallVector<std::pair<unsigned, unsigned>> sharingPairs;
    for (const auto &[fragment, owners] : fragmentOwnerLists)
      for (size_t i = 0; i != owners.size(); ++i)
        for (size_t j = i + 1; j != owners.size(); ++j)
          sharingPairs.emplace_back(owners[i], owners[j]);
    llvm::sort(sharingPairs);
    sharingPairs.erase(std::unique(sharingPairs.begin(), sharingPairs.end()),
                       sharingPairs.end());
    for (auto [first, second] : sharingPairs) {
      auto shared = llvm::find_if(ownerFragments[first], [&](uint32_t id) {
        return llvm::binary_search(ownerFragments[second], id);
      });
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
                         directFragments, computeGraph, activationInputs);

  // The dispatcher selects the lowest pending bit. Give it the same refined
  // dependence order as the helpers, including original-executor boundaries.
  // Otherwise a ready consumer in a later segment can outrank its producers
  // and force another activation even when the component is acyclic.
  // Preserve slots of clocked and unrepresented owners; this only permutes
  // Active combinational work (IEEE 1800-2023 4.5--4.7). SCC backedges remain
  // live and can republish earlier bits; this is not an execute-once promise.
  SmallVector<uint32_t> slots, ranked;
  for (const auto &node : result.rankedNodes)
    if (!node.clocked) {
      slots.push_back(node.owner);
      ranked.push_back(node.owner);
    }
  llvm::sort(slots);
  SmallVector<uint32_t> order(result.mergedFragments.size());
  std::iota(order.begin(), order.end(), 0u);
  for (auto [slot, owner] : llvm::zip_equal(slots, ranked))
    order[slot] = owner;
  reorderOwners(result, order);

  // Owners that resume in the Inactive region (IEEE 1800-2023 4.4.2.3) take
  // the highest bits, in their existing relative order. The lowest pending
  // bit is then deferred only when no Active owner is pending. They execute
  // alone: no executor may consume another owner's deferred activation.
  if (llvm::is_contained(result.deferredOwners, true)) {
    for (auto [owner, deferred] : llvm::enumerate(result.deferredOwners)) {
      if (!deferred)
        continue;
      uint32_t bit = result.mergedFragments[owner].bit;
      bool shared = !result.ownerSubsumptionMasks[owner].isZero() ||
                    llvm::any_of(result.ownerSubsumptionMasks,
                                 [&](const APInt &mask) { return mask[bit]; });
      if (shared)
        return module.emitError(
                   "a deferred eval owner cannot share an executor"),
               failure();
    }
    SmallVector<uint32_t> partition(result.mergedFragments.size());
    std::iota(partition.begin(), partition.end(), 0u);
    std::stable_partition(
        partition.begin(), partition.end(),
        [&](uint32_t owner) { return !result.deferredOwners[owner]; });
    reorderOwners(result, partition);
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
  // May-taint is a finite union lattice over NBA roots. Index reverse
  // dependencies once and revisit only predecessors of a changed fact.
  DenseMap<uint32_t, SmallVector<uint32_t>> predecessors;
  for (Attribute attribute : computeGraph.getEdges()) {
    auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == schedule::ComputeEdgeKind::Sensitivity ||
        edge.getKind() == schedule::ComputeEdgeKind::Resume ||
        edge.getKind() == schedule::ComputeEdgeKind::Spawn)
      predecessors[edge.getTarget()].push_back(edge.getSource());
  }
  SmallVector<uint32_t> pending;
  DenseSet<uint32_t> queued;
  for (const auto &entry : fragmentNBARoots)
    if (llvm::any_of(entry.second, [](uint64_t word) { return word != 0; })) {
      pending.push_back(entry.first);
      queued.insert(entry.first);
    }
  while (!pending.empty()) {
    uint32_t target = pending.pop_back_val();
    queued.erase(target);
    // Inserting a predecessor may rehash fragmentNBARoots.
    auto roots = fragmentNBARoots.lookup(target);
    for (uint32_t predecessor : predecessors.lookup(target)) {
      auto &source = fragmentNBARoots[predecessor];
      source.resize(result.nbaTaintWordCount, 0);
      bool changed = false;
      for (uint32_t word = 0; word != result.nbaTaintWordCount; ++word) {
        uint64_t merged = source[word] | roots[word];
        changed |= merged != source[word];
        source[word] = merged;
      }
      if (changed && queued.insert(predecessor).second)
        pending.push_back(predecessor);
    }
  }
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
  // LRM 4.9.4 and 10.4.2: local pending values belong to captured NBA
  // statements, never to ordinary current-state reads. Revalidate the source
  // graph revision and require complete writer coverage by non-bootstrap
  // generated continuations. A private batch certificate alone is insufficient.
  unsigned pendingCandidates = 0, pendingWriters = 0;
  unsigned pendingBootstrap = 0, pendingCheckpoint = 0, pendingTier2 = 0;
  unsigned pendingMissingOwner = 0, pendingMissingFragment = 0;
  if (activationInputs.sourceGraph == computeGraph &&
      activationInputs.completeNBAWriters &&
      staticNBAPlan.completeSourceWriters) {
    result.transaction.sourceGraph = computeGraph;
    for (auto [index, root] : llvm::enumerate(staticNBAPlan.roots)) {
      if (root.bit_width > 64 ||
          index >= staticNBAPlan.rootDescriptors.size() ||
          !staticNBAPlan.mergeSafeRoots[index] ||
          !staticNBAPlan.privateInfluenceRoots[index])
        continue;
      // runtimeQueueRoots certifies that a shared fallback queue is available;
      // it does not say the generated producer uses that queue. The lowering
      // certificate below must cover every source site with its actual fixed
      // capture form (LRM 4.9.4/10.4.2), including all outlined variants.
      ++pendingCandidates;
      uint64_t descriptor = staticNBAPlan.rootDescriptors[index];
      auto writers = activationInputs.nbaWriters.find(descriptor);
      if (writers == activationInputs.nbaWriters.end() ||
          writers->second.empty())
        continue;
      ++pendingWriters;
      NativeEvalTransactionRoot planned{
          descriptor,
          static_cast<uint32_t>(index),
          root.static_state,
          staticNBAPlan.generatedOffsets[index],
          root.bit_width,
          staticNBAPlan.generatedFixedWriteMasks[index],
          OBELISK_RT_REGION_NBA,
          staticNBAPlan.trackTransients[index],
          writers->second,
          {},
          {}};
      for (const auto &site : staticNBAPlan.sites)
        if (site.root == index) {
          auto origin = staticNBAPlan.siteSemanticOrigins.find(site.site);
          planned.sites.push_back(
              origin == staticNBAPlan.siteSemanticOrigins.end()
                  ? site.site
                  : origin->second);
        }
      llvm::sort(planned.sites);
      planned.sites.erase(
          std::unique(planned.sites.begin(), planned.sites.end()),
          planned.sites.end());
      bool covered = llvm::all_of(planned.writerFragments, [&](uint32_t id) {
        bool found = false;
        bool matched = false;
        for (const auto &direct : directFragments) {
          if (!llvm::is_contained(direct.fragmentIDs, id))
            continue;
          matched = true;
          if (direct.initialActivation || direct.runtimeCheckpoint ||
              direct.tier2Convergence) {
            pendingBootstrap += direct.initialActivation;
            pendingCheckpoint += direct.runtimeCheckpoint;
            pendingTier2 += direct.tier2Convergence;
            continue;
          }
          auto owner = llvm::find(result.mergedExecutors, direct.wrapper);
          if (owner == result.mergedExecutors.end()) {
            ++pendingMissingOwner;
            continue;
          }
          planned.owners.push_back(
              static_cast<uint32_t>(owner - result.mergedExecutors.begin()));
          found = true;
        }
        pendingMissingFragment += !matched;
        return found;
      });
      if (!covered)
        continue;
      llvm::sort(planned.owners);
      planned.owners.erase(
          std::unique(planned.owners.begin(), planned.owners.end()),
          planned.owners.end());
      result.transaction.roots.push_back(std::move(planned));
    }
  }
  if (native::has<native::DebugTiming>(module))
    llvm::errs() << "obelisk eval transaction source roots: "
                 << result.transaction.roots.size() << "/"
                 << staticNBAPlan.roots.size()
                 << " graph=" << (activationInputs.sourceGraph == computeGraph)
                 << " nba-writers=" << activationInputs.completeNBAWriters
                 << " complete-writers=" << staticNBAPlan.completeSourceWriters
                 << " candidates=" << pendingCandidates
                 << " writers=" << pendingWriters
                 << " writer-descriptors=" << activationInputs.nbaWriters.size()
                 << " bootstrap=" << pendingBootstrap
                 << " checkpoint=" << pendingCheckpoint
                 << " tier2=" << pendingTier2
                 << " missing-owner=" << pendingMissingOwner
                 << " missing-fragment=" << pendingMissingFragment << '\n';
  return result;
}

} // namespace obelisk::detail
