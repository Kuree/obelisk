//===- SimulationScheduleAnalysis.cpp - Shared schedule ranks ------------===//

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include <limits>

using namespace mlir;

namespace obelisk::analysis {
namespace {

bool isObserverCaptureBridge(Block &block) {
  if (block.getOperations().size() != 1)
    return false;
  auto branch = dyn_cast<cf::BranchOp>(block.getTerminator());
  return branch && branch->hasAttr("obelisk_sim.observer_capture_bridge");
}

bool isScheduledRegion(sim::ComputeRegionKind kind) {
  return kind == sim::ComputeRegionKind::Active ||
         kind == sim::ComputeRegionKind::Observed ||
         kind == sim::ComputeRegionKind::Reactive ||
         kind == sim::ComputeRegionKind::Postponed;
}

} // namespace

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
  uint32_t fallback = 0;
  design.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      return;
    result.entryRanks[function.getOperation()] = fallback;
    for (Block &block : function.getBody())
      result.blockRanks[&block] = fallback;
    if (fallback != std::numeric_limits<uint32_t>::max())
      ++fallback;
  });

  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph)
    return result;
  ArrayAttr nodes = graph.getNodes();
  uint32_t rank = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = dyn_cast<sim::ComputeRegionAttr>(regionAttribute);
    if (!region || !isScheduledRegion(region.getKind()))
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = dyn_cast<sim::ComputeGroupAttr>(groupAttribute);
      if (!group)
        continue;
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          continue;
        auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
            nodes[static_cast<size_t>(member)]);
        if (!fragment)
          continue;
        sim::SimFuncOp function = design.lookupSymbol<sim::SimFuncOp>(
            fragment.getFunction().getValue());
        if (!function)
          continue;
        Block *block = lookupComputeGraphBlock(function, fragment.getBlock());
        if (!block)
          return function.emitOpError(
              "compute-graph fragment block is out of range");
        result.blockRanks[block] = rank;
        if (fragment.getBlock() == 0)
          result.entryRanks[function.getOperation()] = rank;
      }
      if (rank != std::numeric_limits<uint32_t>::max())
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
