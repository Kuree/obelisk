//===- SimulationScheduleAnalysis.cpp - Shared schedule ranks ------------===//

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <limits>
#include <map>

using namespace mlir;

namespace obelisk::analysis {
namespace {

bool isObserverCaptureBridge(Block &block) {
  if (block.getOperations().size() != 1)
    return false;
  auto branch = dyn_cast<cf::BranchOp>(block.getTerminator());
  return branch &&
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::ObserverCaptureBridge>(branch);
}

bool isScheduledRegion(schedule::ComputeRegionKind kind) {
  return kind == schedule::ComputeRegionKind::Active ||
         kind == schedule::ComputeRegionKind::Observed ||
         kind == schedule::ComputeRegionKind::Reactive ||
         kind == schedule::ComputeRegionKind::Postponed;
}

} // namespace

bool isSettlingEntryKind(sim::EntryKind kind) {
  return kind == sim::EntryKind::AlwaysComb ||
         kind == sim::EntryKind::AlwaysLatch ||
         kind == sim::EntryKind::Continuous ||
         kind == sim::EntryKind::PortInput ||
         kind == sim::EntryKind::PortOutput;
}

SmallVector<StartupPhase> collectStartupPhases(
    sim::SimDesignOp design,
    llvm::function_ref<std::optional<uint32_t>(sim::SimFuncOp)> entryID) {
  llvm::StringMap<sim::SimFuncOp> functions;
  for (auto function : design.getBody().getOps<sim::SimFuncOp>())
    functions[function.getSymName()] = function;
  SmallVector<StartupPhase> phases;
  for (auto root : design.getBody().getOps<sim::SimFuncOp>()) {
    if (root.getEntryKind() != sim::EntryKind::RootInitializer)
      continue;
    for (Block &block : root.getBody()) {
      // Cross-region dependencies are enforced by the event loop. Separate
      // barriers also keep distinct root blocks from acquiring new ordering.
      std::map<sim::EventRegion, StartupPhase> byRegion;
      for (auto spawn : block.getOps<sim::SimSpawnOp>()) {
        auto target = functions.lookup(spawn.getCallee());
        if (!target || target.getBody().empty())
          continue;
        auto id = entryID(target);
        if (!id)
          continue;
        if (sim::isStartupEntryKind(target.getEntryKind()) &&
            !schedule::has<sim::startupWithoutSuspensionAttrName>(target))
          byRegion[target.getHomeRegion()].startups.push_back(*id);
        else if (target.getEntryKind() == sim::EntryKind::Initial)
          byRegion[target.getHomeRegion()].initials.push_back(*id);
      }
      for (auto &[region, phase] : byRegion) {
        if (phase.startups.empty() || phase.initials.empty())
          continue;
        for (auto *ids : {&phase.startups, &phase.initials}) {
          llvm::sort(*ids);
          ids->erase(std::unique(ids->begin(), ids->end()), ids->end());
        }
        phases.push_back(std::move(phase));
      }
    }
  }
  return phases;
}

SmallVector<schedule::ComputeEdgeAttr> projectActivationSchedulingEdges(
    ArrayRef<schedule::ComputeEdgeAttr> edges,
    llvm::function_ref<bool(uint32_t)> isSettlingSource) {
  // A sensitivity edge ends at the wait; execution starts at its resume
  // continuation. Project the publication, not the wait's repeating backedge.
  // This is shared with graph construction so rank refinement cannot omit
  // boundary consumers or invent a different activation graph.
  SmallVector<schedule::ComputeEdgeAttr> projected(edges.begin(), edges.end());
  DenseMap<uint32_t, SmallVector<uint32_t>> continuations;
  for (auto edge : edges)
    if (edge.getKind() == schedule::ComputeEdgeKind::Resume)
      continuations[edge.getSource()].push_back(edge.getTarget());
  for (auto edge : edges) {
    if (edge.getKind() != schedule::ComputeEdgeKind::Sensitivity ||
        !isSettlingSource(edge.getSource()))
      continue;
    for (uint32_t continuation : continuations[edge.getTarget()])
      projected.push_back(schedule::ComputeEdgeAttr::get(
          edge.getContext(), edge.getSource(), continuation,
          schedule::ComputeEdgeKind::Sensitivity, edge.getResource()));
  }
  return projected;
}

Block *lookupComputeGraphBlock(sim::SimFuncOp function, uint32_t ordinal) {
  for (Block &block : function.getBody()) {
    if (isObserverCaptureBridge(block))
      continue;
    if (ordinal-- == 0)
      return &block;
  }
  return nullptr;
}

FailureOr<SimulationScheduleAnalysis>
SimulationScheduleAnalysis::compute(ModuleOp module) {
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  if (!design)
    return SimulationScheduleAnalysis{};
  return compute(design);
}

FailureOr<SimulationScheduleAnalysis>
SimulationScheduleAnalysis::compute(sim::SimDesignOp design) {
  SimulationScheduleAnalysis result;
  llvm::StringMap<sim::SimFuncOp> functions;
  llvm::DenseMap<Operation *, SmallVector<Block *>> graphBlocks;
  uint32_t fallback = 0;
  for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>()) {
    functions[function.getSymName()] = function;
    SmallVector<Block *> &blocks = graphBlocks[function.getOperation()];
    blocks.reserve(function.getBody().getBlocks().size());
    for (Block &block : function.getBody())
      if (!isObserverCaptureBridge(block))
        blocks.push_back(&block);
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      continue;
    result.entryRanks[function.getOperation()] = fallback;
    for (Block &block : function.getBody())
      result.blockRanks[&block] = fallback;
    if (fallback != std::numeric_limits<uint32_t>::max())
      ++fallback;
  }

  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph)
    return result;
  ArrayAttr nodes = graph.getNodes();
  // Refine convergence ties using the complete activation graph, including
  // boundary actors. Helper-only dependencies cannot define global priority.
  // Region/group order, procedural control loops and ownership stay intact.
  SmallVector<uint32_t> groupOf, position;
  uint32_t nextGroup = 0;
  auto refine = [](schedule::ComputeGroupAttr group) {
    return group.getSchedule() == schedule::ComputeScheduleKind::Convergence &&
           group.getFragments().size() > 1;
  };
  for (Attribute rawRegion : graph.getRegions()) {
    auto region = cast<schedule::ComputeRegionAttr>(rawRegion);
    if (!isScheduledRegion(region.getKind()))
      continue;
    for (Attribute rawGroup : region.getGroups()) {
      auto group = cast<schedule::ComputeGroupAttr>(rawGroup);
      if (!refine(group))
        continue;
      if (groupOf.empty())
        groupOf.assign(nodes.size(), UINT32_MAX);
      for (int64_t member : group.getFragments().asArrayRef())
        groupOf[member] = nextGroup;
      ++nextGroup;
    }
  }
  if (nextGroup) {
    SmallVector<schedule::ComputeEdgeAttr> graphEdges;
    for (Attribute raw : graph.getEdges())
      graphEdges.push_back(cast<schedule::ComputeEdgeAttr>(raw));
    auto projected =
        projectActivationSchedulingEdges(graphEdges, [&](uint32_t source) {
          auto fragment =
              dyn_cast<schedule::ComputeFragmentAttr>(nodes[source]);
          auto function =
              fragment ? functions.lookup(fragment.getFunction().getValue())
                       : sim::SimFuncOp{};
          return function && isSettlingEntryKind(function.getEntryKind());
        });
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    for (auto edge : projected)
      if (edge.getKind() != schedule::ComputeEdgeKind::Resume &&
          edge.getKind() != schedule::ComputeEdgeKind::Spawn &&
          groupOf[edge.getSource()] != UINT32_MAX &&
          groupOf[edge.getSource()] == groupOf[edge.getTarget()])
        edges.emplace_back(edge.getSource(), edge.getTarget());
    llvm::StringMap<uint32_t> entries;
    for (Attribute raw : nodes)
      if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
          fragment && fragment.getBlock() == 0)
        entries[fragment.getFunction().getValue()] = fragment.getId();
    auto phases = collectStartupPhases(
        design, [&](sim::SimFuncOp function) -> std::optional<uint32_t> {
          auto found = entries.find(function.getSymName());
          return found == entries.end() ? std::nullopt
                                        : std::optional(found->second);
        });
    SmallVector<StartupPhase> groupedPhases;
    for (const auto &phase : phases) {
      std::map<uint32_t, StartupPhase> byGroup;
      for (uint32_t id : phase.startups)
        if (groupOf[id] != UINT32_MAX)
          byGroup[groupOf[id]].startups.push_back(id);
      for (uint32_t id : phase.initials)
        if (groupOf[id] != UINT32_MAX)
          byGroup[groupOf[id]].initials.push_back(id);
      for (const auto &[group, members] : byGroup) {
        if (members.startups.empty() || members.initials.empty())
          continue;
        groupedPhases.push_back(members);
      }
    }
    // Each refined group is already an activation SCC. ActivationOrder's
    // within-SCC tie-break is reverse DFS finish order with sorted successors.
    // Traverse the SAME logical successors here without expanding the phase
    // product. A shared cursor skips targets already discovered by any source;
    // skipped edges cannot affect DFS finish order. A virtual barrier's DFS
    // order would instead move some initial entries ahead of startup entries.
    SmallVector<SmallVector<uint32_t>> successors(nodes.size());
    for (auto [source, target] : edges)
      successors[source].push_back(target);
    for (auto &targets : successors) {
      llvm::sort(targets);
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
    SmallVector<SmallVector<unsigned>> phasesForSource(nodes.size());
    for (auto [index, phase] : llvm::enumerate(groupedPhases))
      for (uint32_t source : phase.startups)
        phasesForSource[source].push_back(index);
    SmallVector<size_t> phaseCursor(groupedPhases.size(), 0);
    SmallVector<bool> seen(nodes.size(), false);
    SmallVector<std::pair<uint32_t, size_t>> walk;
    position.resize(nodes.size());
    uint32_t remaining = nodes.size();
    for (uint32_t root = 0; root < nodes.size(); ++root) {
      if (seen[root])
        continue;
      seen[root] = true;
      walk.emplace_back(root, 0);
      while (!walk.empty()) {
        auto &[node, cursor] = walk.back();
        auto &targets = successors[node];
        while (cursor < targets.size() && seen[targets[cursor]])
          ++cursor;
        uint32_t next = cursor < targets.size() ? targets[cursor] : UINT32_MAX;
        for (unsigned phase : phasesForSource[node]) {
          auto &initials = groupedPhases[phase].initials;
          auto &index = phaseCursor[phase];
          while (index < initials.size() && seen[initials[index]])
            ++index;
          if (index < initials.size())
            next = std::min(next, initials[index]);
        }
        if (next == UINT32_MAX) {
          position[node] = --remaining;
          walk.pop_back();
        } else {
          seen[next] = true;
          walk.emplace_back(next, 0);
        }
      }
    }
  }
  uint32_t rank = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = dyn_cast<schedule::ComputeRegionAttr>(regionAttribute);
    if (!region || !isScheduledRegion(region.getKind()))
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = dyn_cast<schedule::ComputeGroupAttr>(groupAttribute);
      if (!group)
        continue;
      ArrayRef<int64_t> members = group.getFragments().asArrayRef();
      SmallVector<int64_t> ordered;
      bool refined = refine(group);
      if (refined) {
        ordered.assign(members.begin(), members.end());
        llvm::sort(ordered, [&](int64_t lhs, int64_t rhs) {
          return position[lhs] < position[rhs];
        });
        members = ordered;
      }
      for (int64_t member : members) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          continue;
        auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(
            nodes[static_cast<size_t>(member)]);
        if (!fragment)
          continue;
        sim::SimFuncOp function =
            functions.lookup(fragment.getFunction().getValue());
        if (!function)
          continue;
        auto blocksIt = graphBlocks.find(function.getOperation());
        if (blocksIt == graphBlocks.end())
          continue;
        ArrayRef<Block *> blocks = blocksIt->second;
        if (fragment.getBlock() >= blocks.size())
          return function.emitOpError(
              "compute-graph fragment block is out of range");
        Block *block = blocks[fragment.getBlock()];
        result.blockRanks[block] = rank;
        if (fragment.getBlock() == 0)
          result.entryRanks[function.getOperation()] = rank;
        if (refined && rank != std::numeric_limits<uint32_t>::max())
          ++rank;
      }
      if (!refined && rank != std::numeric_limits<uint32_t>::max())
        ++rank;
    }
  }
  return result;
}

std::optional<uint32_t>
SimulationScheduleAnalysis::getEntryRank(Operation *function) const {
  auto found = entryRanks.find(function);
  if (found == entryRanks.end())
    return std::nullopt;
  return found->second;
}

std::optional<uint32_t>
SimulationScheduleAnalysis::getBlockRank(Block *block) const {
  auto found = blockRanks.find(block);
  if (found == blockRanks.end())
    return std::nullopt;
  return found->second;
}

} // namespace obelisk::analysis
