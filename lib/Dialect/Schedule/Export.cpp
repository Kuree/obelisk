#include "obelisk/Dialect/Schedule/Export.h"
using namespace mlir;
using namespace llvm;
namespace obelisk::schedule {
namespace {
json::Array numbers(DenseI64ArrayAttr values) {
  json::Array result;
  for (int64_t value : values.asArrayRef())
    result.push_back(value);
  return result;
}
json::Object effect(ComputeEffectAttr value) {
  // Resource ranges are 64-bit, beyond JavaScript's exact integer range.
  return json::Object{
      {"effect", stringifyComputeEffectKind(value.getEffect())},
      {"resource", stringifyComputeResourceKind(value.getResource())},
      {"target", stringifyComputeTargetKind(value.getTarget())},
      {"descriptor", std::to_string(value.getDescriptor())},
      {"formal", value.getFormal()},
      {"low", std::to_string(value.getLow())},
      {"width", std::to_string(value.getWidth())},
      {"dynamic", value.getDynamic()},
      {"deferred", value.getDeferred()},
      {"trigger", stringifyComputeTriggerKind(value.getTrigger())}};
}
json::Array effects(ArrayAttr values) {
  json::Array result;
  for (Attribute value : values)
    result.push_back(effect(cast<ComputeEffectAttr>(value)));
  return result;
}
} // namespace
json::Object exportGraph(
    ComputeGraphAttr graph, StringRef name,
    llvm::function_ref<FileLineColLoc(ComputeFragmentAttr)> sourceLocation) {
  json::Array nodes, edges, regions;
  for (auto [index, attr] : llvm::enumerate(graph.getNodes())) {
    json::Object node{{"index", static_cast<int64_t>(index)}};
    if (auto fragment = dyn_cast<ComputeFragmentAttr>(attr)) {
      node["id"] = fragment.getId();
      node["type"] = "fragment";
      node["function"] = fragment.getFunction().getValue();
      node["block"] = fragment.getBlock();
      node["region"] = stringifyComputeRegionKind(fragment.getRegion());
      node["action"] = stringifyComputeActionKind(fragment.getAction());
      node["tier"] = stringifyComputeTierKind(fragment.getTier());
      node["cost"] = std::to_string(fragment.getCost());
      node["lane"] = fragment.getLane();
      node["twoState"] = fragment.getTwoState();
      node["effects"] = static_cast<int64_t>(fragment.getEffects().size());
      node["effectDetails"] = effects(fragment.getEffects());
      if (auto loc = sourceLocation(fragment))
        node["location"] = json::Object{{"file", loc.getFilename().getValue()},
                                        {"line", loc.getLine()},
                                        {"column", loc.getColumn()}};
      else
        node["location"] = nullptr;
    } else if (auto commit = dyn_cast<ComputeNBACommitAttr>(attr)) {
      node["id"] = commit.getId();
      node["type"] = "nba_commit";
      node["slots"] = numbers(commit.getSlots());
      node["accumulatorSites"] = numbers(commit.getAccumulatorSites());
      node["frontierSites"] = numbers(commit.getFrontierSites());
      node["effectDetails"] = effect(commit.getEffect());
      node["tier"] = "generated";
      node["action"] = "commit";
    } else if (auto commit = dyn_cast<ComputeEventCommitAttr>(attr)) {
      node["id"] = commit.getId();
      node["type"] = "event_commit";
      node["sites"] = numbers(commit.getSites());
      node["effectDetails"] = effect(commit.getEffect());
      node["tier"] = "generated";
      node["action"] = "commit";
    }
    nodes.push_back(std::move(node));
  }
  for (Attribute attr : graph.getEdges()) {
    auto edge = cast<ComputeEdgeAttr>(attr);
    json::Object entry{{"source", edge.getSource()},
                       {"target", edge.getTarget()},
                       {"kind", stringifyComputeEdgeKind(edge.getKind())}};
    if (auto resource = edge.getResource()) {
      entry["effectDetails"] = effect(resource);
      entry["resource"] =
          (stringifyComputeResourceKind(resource.getResource()) + " #" +
           Twine(resource.getDescriptor()) + " [" + Twine(resource.getLow()) +
           ":" + Twine(resource.getWidth()) + "]")
              .str();
    } else
      entry["resource"] = "";
    edges.push_back(std::move(entry));
  }
  for (Attribute attr : graph.getRegions()) {
    auto region = cast<ComputeRegionAttr>(attr);
    json::Array groups;
    for (Attribute attr : region.getGroups()) {
      auto group = cast<ComputeGroupAttr>(attr);
      groups.push_back(json::Object{
          {"members", numbers(group.getFragments())},
          {"schedule", stringifyComputeScheduleKind(group.getSchedule())},
          {"feedback", static_cast<int64_t>(group.getFeedback().size())},
          {"feedbackDetails", effects(group.getFeedback())}});
    }
    regions.push_back(
        json::Object{{"kind", stringifyComputeRegionKind(region.getKind())},
                     {"groups", std::move(groups)}});
  }
  return json::Object{{"name", name},
                      {"version", graph.getVersion()},
                      {"vpi", stringifyComputeVPIMode(graph.getVpi())},
                      {"workers", graph.getWorkers()},
                      {"nodes", std::move(nodes)},
                      {"edges", std::move(edges)},
                      {"regions", std::move(regions)}};
}
} // namespace obelisk::schedule
