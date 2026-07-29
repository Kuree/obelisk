//===- StaticSpecializationAnalysis.cpp - Validate static plans ----------===//

#include "obelisk/Analysis/StaticSpecializationAnalysis.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::analysis {

FailureOr<StaticSpecializationAnalysis>
StaticSpecializationAnalysis::compute(sim::SimDesignOp design) {
  StaticSpecializationAnalysis result;
  result.plan = design->getAttrOfType<sim::StaticSpecializationAttr>(
      sim::metadata::staticSpecialization);
  if (!result.plan)
    return result;

  if (result.plan.getSourceGraph() != design.getComputeGraphAttr())
    return design.emitOpError(
               "static-specialization metadata is stale for this design"),
           failure();

  for (Attribute attribute : result.plan.getRoots()) {
    auto root = dyn_cast<sim::StaticStateRootAttr>(attribute);
    if (!root || !result.roots.try_emplace(root.getDescriptor(), root).second)
      return design.emitOpError(
                 "static-specialization root inventory is malformed"),
             failure();
  }

  llvm::DenseSet<uint64_t> plannedNBARoots;
  for (int64_t descriptor : result.plan.getNbaRoots().asArrayRef()) {
    if (descriptor < 0)
      return design.emitOpError(
                 "static-specialization NBA root inventory is malformed"),
             failure();
    uint64_t root = static_cast<uint64_t>(descriptor);
    auto policy = result.roots.find(root);
    if (!plannedNBARoots.insert(root).second || policy == result.roots.end() ||
        !policy->second.getNba())
      return design.emitOpError(
                 "static-specialization NBA root inventory is malformed"),
             failure();
    result.nbaRoots.push_back(root);
  }
  if (llvm::any_of(result.roots, [&](const auto &entry) {
        return entry.second.getNba() && !plannedNBARoots.contains(entry.first);
      }))
    return design.emitOpError(
               "static-specialization NBA root is absent from its ordered "
               "inventory"),
           failure();

  ArrayAttr nodes = result.plan.getSourceGraph().getNodes();
  llvm::DenseSet<uint32_t> seenCommits;
  for (Attribute regionAttribute : result.plan.getSourceGraph().getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::NBA)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      if (group.getSchedule() != sim::ComputeScheduleKind::Acyclic)
        return design.emitOpError(
                   "static-specialization requires acyclic NBA groups"),
               failure();
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          return design.emitOpError(
                     "static-specialization NBA group references an invalid "
                     "node"),
                 failure();
        auto commit = dyn_cast<sim::ComputeNBACommitAttr>(
            nodes[static_cast<size_t>(member)]);
        if (!commit)
          continue;
        if (!seenCommits.insert(commit.getId()).second)
          return design.emitOpError(
                     "static-specialization NBA commit is scheduled twice"),
                 failure();
        result.orderedNBACommits.push_back(commit);
      }
    }
  }
  size_t commitCount = llvm::count_if(nodes, [](Attribute node) {
    return isa<sim::ComputeNBACommitAttr>(node);
  });
  if (result.orderedNBACommits.size() != commitCount)
    return design.emitOpError(
               "static-specialization schedule omits an NBA commit"),
           failure();

  llvm::DenseSet<uint64_t> foundRoots;
  for (sim::ComputeNBACommitAttr commit : result.orderedNBACommits) {
    uint64_t descriptor = commit.getEffect().getDescriptor();
    if (!plannedNBARoots.contains(descriptor))
      continue;
    if (!commit.getFrontierSites().empty() ||
        !foundRoots.insert(descriptor).second)
      return design.emitOpError(
                 "static-specialization NBA root disagrees with its source "
                 "graph"),
             failure();
    auto appendSites = [&](DenseI64ArrayAttr sites) -> LogicalResult {
      for (int64_t site : sites.asArrayRef())
        if (site < 0 ||
            !result.nbaSites.insert(static_cast<uint64_t>(site)).second)
          return design.emitOpError(
              "static-specialization NBA site inventory is malformed");
      return success();
    };
    if (failed(appendSites(commit.getSlots())) ||
        failed(appendSites(commit.getAccumulatorSites())))
      return failure();
  }
  if (foundRoots.size() != plannedNBARoots.size())
    return design.emitOpError(
               "static-specialization NBA root is absent from its source "
               "graph"),
           failure();
  return result;
}

} // namespace obelisk::analysis
