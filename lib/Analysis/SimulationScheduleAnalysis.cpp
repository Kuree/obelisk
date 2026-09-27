//===- SimulationScheduleAnalysis.cpp - Shared schedule ranks ------------===//

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Runtime/ActivationOrder.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <limits>

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
    runtime::ActivationOrder order;
    if (!runtime::ActivationOrder::build(nodes.size(), std::move(edges), order))
      return design.emitOpError("invalid activation edge in schedule ranks");
    position.resize(nodes.size());
    for (auto [rank, node] : llvm::enumerate(order.nodes))
      position[node] = rank;
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
