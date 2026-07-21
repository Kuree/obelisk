//===- VerifyComputeGraph.cpp - Validate late schedule metadata ----------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMVERIFYCOMPUTEGRAPHPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

enum class NodeKind { Fragment, NBACommit, EventCommit };

static std::optional<uint64_t> getNonnegativeU64(IntegerAttr attribute) {
  if (!attribute || attribute.getValue().isNegative() ||
      attribute.getValue().getBitWidth() > 64)
    return std::nullopt;
  return attribute.getValue().getZExtValue();
}

static std::optional<uint64_t> getValueWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<sim::LogicType>(type))
    return logic.getWidth();
  if (auto reference = dyn_cast<sim::RefType>(type))
    return getValueWidth(reference.getElementType());
  if (auto net = dyn_cast<sim::NetType>(type))
    return getValueWidth(net.getElementType());
  if (auto driver = dyn_cast<sim::DriverType>(type))
    return getValueWidth(driver.getElementType());
  if (isa<sim::EventType>(type))
    return 1;
  return std::nullopt;
}

static sim::ComputeResourceKind getResourceKind(Type type) {
  if (isa<sim::RefType>(type))
    return sim::ComputeResourceKind::Storage;
  if (isa<sim::NetType, sim::DriverType>(type))
    return sim::ComputeResourceKind::Net;
  if (isa<sim::EventType>(type))
    return sim::ComputeResourceKind::Event;
  return sim::ComputeResourceKind::Unknown;
}

struct DescriptorInventory {
  DenseMap<uint64_t, uint64_t> storage;
  DenseMap<uint64_t, uint64_t> nets;
  DenseMap<uint64_t, uint64_t> events;
};

static LogicalResult verifyEffect(sim::SimDesignOp design,
                                  sim::ComputeEffectAttr effect,
                                  const DescriptorInventory &inventory,
                                  sim::SimFuncOp function = {}) {
  if (!effect)
    return design.emitOpError("contains a null compute effect");
  uint64_t rootWidth = 0;
  if (effect.getTarget() == sim::ComputeTargetKind::Descriptor) {
    uint64_t descriptor = effect.getDescriptor();
    const DenseMap<uint64_t, uint64_t> *descriptors = nullptr;
    switch (effect.getResource()) {
    case sim::ComputeResourceKind::Storage:
      descriptors = &inventory.storage;
      break;
    case sim::ComputeResourceKind::Net:
      descriptors = &inventory.nets;
      break;
    case sim::ComputeResourceKind::Event:
      descriptors = &inventory.events;
      break;
    default:
      return design.emitOpError("effect descriptor has no concrete resource");
    }
    auto found = descriptors->find(descriptor);
    if (found == descriptors->end())
      return design.emitOpError("effect references a missing descriptor");
    rootWidth = found->second;
  } else if (effect.getTarget() == sim::ComputeTargetKind::Formal) {
    if (!function ||
        static_cast<uint64_t>(effect.getFormal()) >= function.getNumArguments())
      return design.emitOpError("effect references a missing formal handle");
    Type type = function.getArgumentTypes()[effect.getFormal()];
    if (getResourceKind(type) != effect.getResource())
      return design.emitOpError("effect formal has the wrong resource kind");
    std::optional<uint64_t> width = getValueWidth(type);
    if (!width)
      return design.emitOpError("effect formal has no packed range");
    rootWidth = *width;
  } else {
    return success();
  }
  if (effect.getLow() > rootWidth ||
      effect.getWidth() > rootWidth - effect.getLow())
    return design.emitOpError("effect range exceeds its root descriptor");
  return success();
}

static LogicalResult verifyEffectArray(sim::SimDesignOp design,
                                       ArrayAttr effects,
                                       const DescriptorInventory &inventory,
                                       sim::SimFuncOp function = {}) {
  for (Attribute attribute : effects) {
    auto effect = dyn_cast<sim::ComputeEffectAttr>(attribute);
    if (!effect || failed(verifyEffect(design, effect, inventory, function)))
      return failure();
  }
  return success();
}

static bool effectsAlias(sim::ComputeEffectAttr lhs,
                         sim::ComputeEffectAttr rhs) {
  if (lhs.getResource() == sim::ComputeResourceKind::Unknown ||
      rhs.getResource() == sim::ComputeResourceKind::Unknown)
    return true;
  if (lhs.getResource() != rhs.getResource())
    return false;
  if (lhs.getTarget() == sim::ComputeTargetKind::Descriptor &&
      rhs.getTarget() == sim::ComputeTargetKind::Descriptor &&
      lhs.getDescriptor() != rhs.getDescriptor())
    return false;
  // Formal handles may alias any handle in their resource class until call or
  // spawn specialization proves otherwise.
  if (lhs.getTarget() != sim::ComputeTargetKind::Descriptor ||
      rhs.getTarget() != sim::ComputeTargetKind::Descriptor)
    return true;
  if (lhs.getDynamic() || rhs.getDynamic())
    return true;
  if (lhs.getLow() > std::numeric_limits<uint64_t>::max() - lhs.getWidth() ||
      rhs.getLow() > std::numeric_limits<uint64_t>::max() - rhs.getWidth())
    return true;
  uint64_t lhsEnd = lhs.getLow() + lhs.getWidth();
  uint64_t rhsEnd = rhs.getLow() + rhs.getWidth();
  return lhs.getLow() < rhsEnd && rhs.getLow() < lhsEnd;
}

static bool isActiveProducer(sim::ComputeEffectAttr effect) {
  return !effect.getDeferred() &&
         (effect.getEffect() == sim::ComputeEffectKind::Write ||
          effect.getEffect() == sim::ComputeEffectKind::Drive ||
          effect.getEffect() == sim::ComputeEffectKind::Trigger);
}

static bool activeEffectsConflict(sim::ComputeEffectAttr lhs,
                                  sim::ComputeEffectAttr rhs) {
  if (lhs.getEffect() == sim::ComputeEffectKind::Watch ||
      rhs.getEffect() == sim::ComputeEffectKind::Watch ||
      lhs.getEffect() == sim::ComputeEffectKind::NBA ||
      rhs.getEffect() == sim::ComputeEffectKind::NBA)
    return false;
  if (!isActiveProducer(lhs) && !isActiveProducer(rhs))
    return false;
  return effectsAlias(lhs, rhs);
}

static bool containsEffect(ArrayAttr effects, sim::ComputeEffectAttr effect) {
  return llvm::is_contained(effects, effect);
}

struct IndexedComputeEffect {
  uint32_t fragment;
  sim::ComputeEffectAttr effect;
};

static bool
hasProceduralControlCycle(const llvm::SmallDenseSet<uint32_t> &members,
                          ArrayRef<sim::ComputeEdgeAttr> edges) {
  DenseMap<uint32_t, SmallVector<uint32_t>> successors;
  DenseMap<uint32_t, unsigned> indegree;
  for (uint32_t member : members)
    indegree.try_emplace(member, 0);
  for (sim::ComputeEdgeAttr edge : edges) {
    if (edge.getKind() != sim::ComputeEdgeKind::ProcessOrder ||
        !members.contains(edge.getSource()) ||
        !members.contains(edge.getTarget()))
      continue;
    successors[edge.getSource()].push_back(edge.getTarget());
    ++indegree[edge.getTarget()];
  }
  SmallVector<uint32_t> ready;
  for (uint32_t member : members)
    if (indegree[member] == 0)
      ready.push_back(member);
  size_t visited = 0;
  while (!ready.empty()) {
    uint32_t member = ready.pop_back_val();
    ++visited;
    for (uint32_t successor : successors[member])
      if (--indegree[successor] == 0)
        ready.push_back(successor);
  }
  return visited != members.size();
}

class ComputeEffectIndex {
public:
  void add(uint32_t fragment, sim::ComputeEffectAttr effect) {
    IndexedComputeEffect indexed{fragment, effect};
    all.push_back(indexed);
    if (effect.getResource() == sim::ComputeResourceKind::Unknown) {
      unknown.push_back(indexed);
      return;
    }
    unsigned resource = static_cast<unsigned>(effect.getResource());
    byResource[resource].push_back(indexed);
    if (effect.getTarget() == sim::ComputeTargetKind::Descriptor)
      exact[{resource, effect.getDescriptor()}].push_back(indexed);
    else
      wildcard[resource].push_back(indexed);
  }

  template <typename Callback>
  void forEachAlias(sim::ComputeEffectAttr effect, Callback &&callback) const {
    auto visit = [&](ArrayRef<IndexedComputeEffect> effects) {
      for (IndexedComputeEffect indexed : effects)
        callback(indexed);
    };
    if (effect.getResource() == sim::ComputeResourceKind::Unknown) {
      visit(all);
      return;
    }
    unsigned resource = static_cast<unsigned>(effect.getResource());
    visit(unknown);
    if (effect.getTarget() != sim::ComputeTargetKind::Descriptor) {
      if (auto found = byResource.find(resource); found != byResource.end())
        visit(found->second);
      return;
    }
    if (auto found = wildcard.find(resource); found != wildcard.end())
      visit(found->second);
    if (auto found = exact.find({resource, effect.getDescriptor()});
        found != exact.end())
      visit(found->second);
  }

private:
  SmallVector<IndexedComputeEffect> all;
  SmallVector<IndexedComputeEffect> unknown;
  std::map<unsigned, SmallVector<IndexedComputeEffect>> byResource;
  std::map<unsigned, SmallVector<IndexedComputeEffect>> wildcard;
  std::map<std::pair<unsigned, uint64_t>, SmallVector<IndexedComputeEffect>>
      exact;
};

static simlowering::DescriptorProvenance
getProvenance(Value value,
              const simlowering::DescriptorProvenanceMap &provenance) {
  auto found = provenance.find(value);
  return found == provenance.end() ? simlowering::DescriptorProvenance{}
                                   : found->second;
}

static simlowering::DescriptorProvenance
getRootProvenance(simlowering::DescriptorProvenance provenance) {
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
    return provenance;
  if (provenance.formal && !provenance.descriptor)
    return {};
  provenance.low = 0;
  provenance.width = provenance.rootWidth;
  provenance.dynamic = false;
  return provenance;
}

static sim::ComputeEffectAttr
getEffect(MLIRContext *context, sim::ComputeEffectKind effect,
          const simlowering::DescriptorProvenance &provenance,
          bool deferred = false) {
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
    return sim::ComputeEffectAttr::get(
        context, effect, sim::ComputeResourceKind::Unknown,
        sim::ComputeTargetKind::Unknown, 0, 0, 0, 0, false, deferred,
        sim::ComputeTriggerKind::None);
  sim::ComputeTargetKind target = sim::ComputeTargetKind::Unknown;
  if (provenance.descriptor)
    target = sim::ComputeTargetKind::Descriptor;
  else if (provenance.formal)
    target = sim::ComputeTargetKind::Formal;
  else if (provenance.resource == sim::ComputeResourceKind::Local)
    target = sim::ComputeTargetKind::Local;
  return sim::ComputeEffectAttr::get(
      context, effect, provenance.resource, target,
      provenance.descriptor.value_or(0), provenance.formal.value_or(0),
      provenance.low, provenance.width, provenance.dynamic, deferred,
      sim::ComputeTriggerKind::None);
}

class ObeliskSimVerifyComputeGraphPass
    : public impl::ObeliskSimVerifyComputeGraphPassBase<
          ObeliskSimVerifyComputeGraphPass> {
public:
  void runOnOperation() override;
};

void ObeliskSimVerifyComputeGraphPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  auto graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError("has no typed compute_graph metadata");
    signalPassFailure();
    return;
  }

  DescriptorInventory inventory;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation))
      inventory.storage[storage.getId()] = *getValueWidth(storage.getType());
    else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation))
      inventory.nets[net.getId()] = *getValueWidth(net.getType());
  }
  design.walk([&](sim::SimContextEventOp event) {
    inventory.events[event.getId()] = 1;
  });
  design.walk([&](sim::SimFuncOp function) {
    for (unsigned index = 0; index != function.getNumArguments(); ++index)
      if (isa<sim::EventType>(function.getArgumentTypes()[index]))
        if (auto descriptor = function.getArgAttrOfType<IntegerAttr>(
                index, "obelisk_sim.descriptor_id"))
          if (std::optional<uint64_t> value = getNonnegativeU64(descriptor))
            inventory.events[*value] = 1;
  });

  sim::ComputeObservabilityKind expectedObservability =
      graph.getVpi() == sim::ComputeVPIMode::Full
          ? sim::ComputeObservabilityKind::ExternallyWritable
      : graph.getVpi() == sim::ComputeVPIMode::Read
          ? sim::ComputeObservabilityKind::SafePoint
          : sim::ComputeObservabilityKind::Invisible;
  bool invalidObservability = false;
  auto verifyObservability = [&](auto descriptor) {
    std::optional<sim::ComputeObservabilityKind> actual =
        descriptor.getObservability();
    if (!actual || *actual != expectedObservability) {
      descriptor.emitOpError("observability does not match compute-graph VPI "
                             "mode");
      invalidObservability = true;
    }
  };
  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    verifyObservability(storage);
  for (sim::SimNetDeclOp net :
       design.getBody().front().getOps<sim::SimNetDeclOp>())
    verifyObservability(net);
  if (invalidObservability) {
    signalPassFailure();
    return;
  }

  ArrayAttr nodes = graph.getNodes();
  SmallVector<NodeKind> nodeKinds(nodes.size());
  SmallVector<sim::ComputeRegionKind> nodeRegions(nodes.size());
  SmallVector<sim::ComputeFragmentAttr> fragmentNodes(nodes.size());
  DenseMap<Block *, uint32_t> fragmentForBlock;
  DenseMap<Operation *, SmallVector<uint32_t>> fragmentsForFunction;
  DenseMap<uint32_t, llvm::SmallDenseSet<uint64_t>> nbaSlots;
  DenseMap<uint32_t, llvm::SmallDenseSet<uint64_t>> nbaAccumulatorSites;
  DenseMap<uint32_t, llvm::SmallDenseSet<uint64_t>> nbaStaticJournalSites;
  DenseMap<uint32_t, llvm::SmallDenseSet<uint64_t>> nbaFrontierSites;
  DenseMap<uint32_t, llvm::SmallDenseSet<uint64_t>> eventSlots;
  llvm::SmallDenseSet<uint64_t> inventoriedNBASites;
  llvm::SmallDenseSet<uint64_t> inventoriedEventSites;
  llvm::SmallDenseSet<Attribute> nbaCommitTargets;
  llvm::SmallDenseSet<Attribute> eventCommitTargets;
  bool duplicateInventorySite = false;
  llvm::SmallDenseSet<uint32_t> nodeIds;

  for (Attribute attribute : nodes) {
    uint32_t id;
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      id = fragment.getId();
      if (id >= nodes.size() || !nodeIds.insert(id).second ||
          fragment.getLane() >= graph.getWorkers()) {
        design.emitOpError(
            "compute graph contains an invalid fragment ID/lane");
        signalPassFailure();
        return;
      }
      auto function = dyn_cast_or_null<sim::SimFuncOp>(
          SymbolTable::lookupSymbolIn(design, fragment.getFunction()));
      if (!function || function.getEntryKind() == sim::EntryKind::Function ||
          fragment.getBlock() >= function.getBody().getBlocks().size()) {
        design.emitOpError("fragment references a missing function or block");
        signalPassFailure();
        return;
      }
      sim::ComputeRegionKind expectedRegion =
          function.getEntryKind() == sim::EntryKind::Final
              ? sim::ComputeRegionKind::Postponed
              : sim::ComputeRegionKind::Active;
      if (fragment.getRegion() != expectedRegion ||
          failed(verifyEffectArray(design, fragment.getEffects(), inventory,
                                   function))) {
        signalPassFailure();
        return;
      }
      Block *block =
          &*std::next(function.getBody().begin(), fragment.getBlock());
      if (!fragmentForBlock.try_emplace(block, id).second) {
        design.emitOpError("multiple fragments reference one CFG block");
        signalPassFailure();
        return;
      }
      fragmentsForFunction[function.getOperation()].push_back(id);
      nodeKinds[id] = NodeKind::Fragment;
      nodeRegions[id] = fragment.getRegion();
      fragmentNodes[id] = fragment;
    } else if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(attribute)) {
      id = commit.getId();
      if (id >= nodes.size() || !nodeIds.insert(id).second ||
          failed(verifyEffect(design, commit.getEffect(), inventory))) {
        signalPassFailure();
        return;
      }
      if (!nbaCommitTargets.insert(commit.getEffect()).second) {
        design.emitOpError("compute graph has a duplicate NBA commit target");
        signalPassFailure();
        return;
      }
      nodeKinds[id] = NodeKind::NBACommit;
      nodeRegions[id] = sim::ComputeRegionKind::NBA;
      for (int64_t site : commit.getSlots().asArrayRef()) {
        nbaSlots[id].insert(site);
        duplicateInventorySite |= !inventoriedNBASites.insert(site).second;
      }
      for (int64_t site : commit.getAccumulatorSites().asArrayRef()) {
        nbaAccumulatorSites[id].insert(site);
        duplicateInventorySite |= !inventoriedNBASites.insert(site).second;
      }
      for (int64_t site : commit.getStaticJournalSites().asArrayRef()) {
        nbaStaticJournalSites[id].insert(site);
        duplicateInventorySite |= !inventoriedNBASites.insert(site).second;
      }
      for (int64_t site : commit.getFrontierSites().asArrayRef()) {
        nbaFrontierSites[id].insert(site);
        duplicateInventorySite |= !inventoriedNBASites.insert(site).second;
      }
    } else if (auto commit = dyn_cast<sim::ComputeEventCommitAttr>(attribute)) {
      id = commit.getId();
      if (id >= nodes.size() || !nodeIds.insert(id).second ||
          failed(verifyEffect(design, commit.getEffect(), inventory))) {
        signalPassFailure();
        return;
      }
      if (!eventCommitTargets.insert(commit.getEffect()).second) {
        design.emitOpError(
            "compute graph has a duplicate deferred-event commit target");
        signalPassFailure();
        return;
      }
      nodeKinds[id] = NodeKind::EventCommit;
      nodeRegions[id] = sim::ComputeRegionKind::NBA;
      for (int64_t site : commit.getSites().asArrayRef()) {
        eventSlots[id].insert(site);
        duplicateInventorySite |= !inventoriedEventSites.insert(site).second;
      }
    } else {
      design.emitOpError("compute graph contains an unknown node attribute");
      signalPassFailure();
      return;
    }
  }
  if (nodeIds.size() != nodes.size()) {
    design.emitOpError("compute-graph node IDs are not dense from zero");
    signalPassFailure();
    return;
  }

  ArrayAttr edges = graph.getEdges();
  SmallVector<sim::ComputeEdgeAttr> typedEdges;
  for (Attribute attribute : edges) {
    auto edge = dyn_cast<sim::ComputeEdgeAttr>(attribute);
    if (!edge || edge.getSource() >= nodes.size() ||
        edge.getTarget() >= nodes.size()) {
      design.emitOpError("compute graph contains an invalid edge endpoint");
      signalPassFailure();
      return;
    }
    switch (edge.getKind()) {
    case sim::ComputeEdgeKind::ProcessOrder:
    case sim::ComputeEdgeKind::Resume: {
      if (nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment) {
        design.emitOpError("CFG edge does not connect fragments");
        signalPassFailure();
        return;
      }
      auto source = fragmentNodes[edge.getSource()];
      auto target = fragmentNodes[edge.getTarget()];
      if (source.getFunction() != target.getFunction()) {
        design.emitOpError("CFG edge crosses function boundaries");
        signalPassFailure();
        return;
      }
      auto function = dyn_cast_or_null<sim::SimFuncOp>(
          SymbolTable::lookupSymbolIn(design, source.getFunction()));
      Block *block = &*std::next(function.getBody().begin(), source.getBlock());
      Block *targetBlock =
          &*std::next(function.getBody().begin(), target.getBlock());
      if (!llvm::is_contained(block->getTerminator()->getSuccessors(),
                              targetBlock)) {
        design.emitOpError("CFG edge does not match an actual successor");
        signalPassFailure();
        return;
      }
      break;
    }
    case sim::ComputeEdgeKind::Spawn: {
      if (nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment ||
          fragmentNodes[edge.getTarget()].getBlock() != 0) {
        design.emitOpError("spawn edge does not target a process entry");
        signalPassFailure();
        return;
      }
      auto source = fragmentNodes[edge.getSource()];
      auto target = fragmentNodes[edge.getTarget()];
      auto function = dyn_cast_or_null<sim::SimFuncOp>(
          SymbolTable::lookupSymbolIn(design, source.getFunction()));
      Block *block = &*std::next(function.getBody().begin(), source.getBlock());
      if (!llvm::any_of(block->getOps<sim::SimSpawnOp>(),
                        [&](sim::SimSpawnOp spawn) {
                          return spawn.getCalleeAttr() == target.getFunction();
                        })) {
        design.emitOpError("spawn edge does not match a spawn operation");
        signalPassFailure();
        return;
      }
      break;
    }
    case sim::ComputeEdgeKind::Sensitivity:
      if (!edge.getResource() ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Watch ||
          nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment ||
          !containsEffect(fragmentNodes[edge.getTarget()].getEffects(),
                          edge.getResource()) ||
          !llvm::any_of(fragmentNodes[edge.getSource()].getEffects(),
                        [&](Attribute attribute) {
                          auto effect = cast<sim::ComputeEffectAttr>(attribute);
                          return isActiveProducer(effect) &&
                                 effectsAlias(effect, edge.getResource());
                        })) {
        design.emitOpError("sensitivity edge has an invalid consumer");
        signalPassFailure();
        return;
      }
      break;
    case sim::ComputeEdgeKind::NBAStage:
      if (nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::NBACommit ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::NBA ||
          !containsEffect(fragmentNodes[edge.getSource()].getEffects(),
                          edge.getResource()) ||
          !effectsAlias(edge.getResource(),
                        cast<sim::ComputeNBACommitAttr>(nodes[edge.getTarget()])
                            .getEffect())) {
        design.emitOpError("NBA stage edge has invalid endpoint kinds");
        signalPassFailure();
        return;
      }
      break;
    case sim::ComputeEdgeKind::NBAActivate:
      if (nodeKinds[edge.getSource()] != NodeKind::NBACommit ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Watch ||
          !containsEffect(fragmentNodes[edge.getTarget()].getEffects(),
                          edge.getResource()) ||
          !effectsAlias(cast<sim::ComputeNBACommitAttr>(nodes[edge.getSource()])
                            .getEffect(),
                        edge.getResource())) {
        design.emitOpError("NBA activation edge has invalid endpoint kinds");
        signalPassFailure();
        return;
      }
      break;
    case sim::ComputeEdgeKind::DeferredStage:
      if (nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::EventCommit ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Trigger ||
          !edge.getResource().getDeferred() ||
          !containsEffect(fragmentNodes[edge.getSource()].getEffects(),
                          edge.getResource()) ||
          !effectsAlias(edge.getResource(), cast<sim::ComputeEventCommitAttr>(
                                                nodes[edge.getTarget()])
                                                .getEffect())) {
        design.emitOpError("deferred event stage edge is invalid");
        signalPassFailure();
        return;
      }
      break;
    case sim::ComputeEdgeKind::DeferredActivate:
      if (nodeKinds[edge.getSource()] != NodeKind::EventCommit ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Watch ||
          !containsEffect(fragmentNodes[edge.getTarget()].getEffects(),
                          edge.getResource()) ||
          !effectsAlias(
              cast<sim::ComputeEventCommitAttr>(nodes[edge.getSource()])
                  .getEffect(),
              edge.getResource())) {
        design.emitOpError("deferred event activation edge is invalid");
        signalPassFailure();
        return;
      }
      break;
    case sim::ComputeEdgeKind::Conflict:
      if (nodeKinds[edge.getSource()] != NodeKind::Fragment ||
          nodeKinds[edge.getTarget()] != NodeKind::Fragment ||
          fragmentNodes[edge.getSource()].getFunction() ==
              fragmentNodes[edge.getTarget()].getFunction() ||
          (!containsEffect(fragmentNodes[edge.getSource()].getEffects(),
                           edge.getResource()) &&
           !containsEffect(fragmentNodes[edge.getTarget()].getEffects(),
                           edge.getResource())) ||
          !llvm::any_of(fragmentNodes[edge.getSource()].getEffects(),
                        [&](Attribute lhsAttribute) {
                          auto lhs = cast<sim::ComputeEffectAttr>(lhsAttribute);
                          return llvm::any_of(
                              fragmentNodes[edge.getTarget()].getEffects(),
                              [&](Attribute rhsAttribute) {
                                auto rhs =
                                    cast<sim::ComputeEffectAttr>(rhsAttribute);
                                return activeEffectsConflict(lhs, rhs);
                              });
                        })) {
        design.emitOpError("conflict edge does not connect fragments");
        signalPassFailure();
        return;
      }
      break;
    }
    typedEdges.push_back(edge);
  }

  DenseMap<std::pair<uint32_t, uint32_t>, uint16_t> edgeKinds;
  using EdgeResourceSets = std::array<llvm::SmallDenseSet<Attribute, 4>, 9>;
  DenseMap<std::pair<uint32_t, uint32_t>, EdgeResourceSets> edgeResources;
  SmallVector<SmallVector<uint32_t>> forwardAdjacency(nodes.size());
  SmallVector<SmallVector<uint32_t>> reverseAdjacency(nodes.size());
  for (sim::ComputeEdgeAttr edge : typedEdges) {
    edgeKinds[{edge.getSource(), edge.getTarget()}] |=
        uint16_t{1} << static_cast<unsigned>(edge.getKind());
    if (edge.getResource())
      edgeResources[{edge.getSource(), edge.getTarget()}]
                   [static_cast<unsigned>(edge.getKind())]
                       .insert(edge.getResource());
    if (edge.getKind() != sim::ComputeEdgeKind::Resume) {
      forwardAdjacency[edge.getSource()].push_back(edge.getTarget());
      reverseAdjacency[edge.getTarget()].push_back(edge.getSource());
    }
  }
  for (auto &adjacency : forwardAdjacency) {
    llvm::sort(adjacency);
    adjacency.erase(std::unique(adjacency.begin(), adjacency.end()),
                    adjacency.end());
  }
  for (auto &adjacency : reverseAdjacency) {
    llvm::sort(adjacency);
    adjacency.erase(std::unique(adjacency.begin(), adjacency.end()),
                    adjacency.end());
  }
  auto hasEdge = [&](uint32_t source, uint32_t target,
                     sim::ComputeEdgeKind kind) {
    auto found = edgeKinds.find({source, target});
    return found != edgeKinds.end() &&
           (found->second & (uint16_t{1} << static_cast<unsigned>(kind))) != 0;
  };
  auto hasResourceEdge = [&](uint32_t source, uint32_t target,
                             sim::ComputeEdgeKind kind,
                             sim::ComputeEffectAttr resource) {
    auto found = edgeResources.find({source, target});
    return found != edgeResources.end() &&
           found->second[static_cast<unsigned>(kind)].contains(resource);
  };
  ComputeEffectIndex watchedEffects, activeEffects;
  for (uint32_t fragment = 0; fragment != fragmentNodes.size(); ++fragment) {
    if (!fragmentNodes[fragment])
      continue;
    for (Attribute attribute : fragmentNodes[fragment].getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(attribute);
      if (effect.getEffect() == sim::ComputeEffectKind::Watch)
        watchedEffects.add(fragment, effect);
      else if (effect.getEffect() != sim::ComputeEffectKind::NBA)
        activeEffects.add(fragment, effect);
    }
  }
  bool missingDependency = false;
  for (uint32_t source = 0;
       source != fragmentNodes.size() && !missingDependency; ++source) {
    if (!fragmentNodes[source])
      continue;
    for (Attribute sourceAttribute : fragmentNodes[source].getEffects()) {
      auto produced = cast<sim::ComputeEffectAttr>(sourceAttribute);
      if (isActiveProducer(produced))
        watchedEffects.forEachAlias(
            produced, [&](IndexedComputeEffect watched) {
              if (!missingDependency &&
                  effectsAlias(produced, watched.effect) &&
                  !hasResourceEdge(source, watched.fragment,
                                   sim::ComputeEdgeKind::Sensitivity,
                                   watched.effect)) {
                design.emitOpError("compute graph omits a sensitivity edge");
                missingDependency = true;
              }
            });
      if (missingDependency ||
          produced.getEffect() == sim::ComputeEffectKind::Watch ||
          produced.getEffect() == sim::ComputeEffectKind::NBA)
        continue;
      activeEffects.forEachAlias(produced, [&](IndexedComputeEffect target) {
        if (missingDependency || target.fragment <= source ||
            fragmentNodes[target.fragment].getFunction() ==
                fragmentNodes[source].getFunction() ||
            !activeEffectsConflict(produced, target.effect))
          return;
        sim::ComputeEffectAttr resource =
            isActiveProducer(produced) ? produced : target.effect;
        if (!hasResourceEdge(source, target.fragment,
                             sim::ComputeEdgeKind::Conflict, resource)) {
          design.emitOpError("compute graph omits a conflict edge");
          missingDependency = true;
        }
      });
    }
  }
  if (missingDependency) {
    signalPassFailure();
    return;
  }
  for (uint32_t source = 0; source != nodes.size(); ++source) {
    sim::ComputeEffectAttr committed;
    sim::ComputeEdgeKind activationKind;
    if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(nodes[source])) {
      committed = commit.getEffect();
      activationKind = sim::ComputeEdgeKind::NBAActivate;
    } else if (auto commit =
                   dyn_cast<sim::ComputeEventCommitAttr>(nodes[source])) {
      committed = commit.getEffect();
      activationKind = sim::ComputeEdgeKind::DeferredActivate;
    } else {
      continue;
    }
    watchedEffects.forEachAlias(committed, [&](IndexedComputeEffect watched) {
      if (effectsAlias(committed, watched.effect) &&
          !hasResourceEdge(source, watched.fragment, activationKind,
                           watched.effect))
        missingDependency = true;
    });
    if (missingDependency) {
      design.emitOpError("compute graph omits a commit activation edge");
      signalPassFailure();
      return;
    }
  }
  for (sim::ComputeFragmentAttr fragment : fragmentNodes) {
    if (!fragment)
      continue;
    auto function = dyn_cast_or_null<sim::SimFuncOp>(
        SymbolTable::lookupSymbolIn(design, fragment.getFunction()));
    Block *block = &*std::next(function.getBody().begin(), fragment.getBlock());
    sim::ComputeEdgeKind controlKind =
        simlowering::isSuspensionTerminator(block->getTerminator())
            ? sim::ComputeEdgeKind::Resume
            : sim::ComputeEdgeKind::ProcessOrder;
    for (Block *successor : block->getTerminator()->getSuccessors()) {
      auto target = fragmentForBlock.find(successor);
      if (target == fragmentForBlock.end() ||
          !hasEdge(fragment.getId(), target->second, controlKind)) {
        design.emitOpError("compute graph omits a CFG edge");
        signalPassFailure();
        return;
      }
    }
    for (sim::SimSpawnOp spawn : block->getOps<sim::SimSpawnOp>()) {
      auto callee = dyn_cast_or_null<sim::SimFuncOp>(
          SymbolTable::lookupSymbolIn(design, spawn.getCalleeAttr()));
      auto target = callee && !callee.getBody().empty()
                        ? fragmentForBlock.find(&callee.getBody().front())
                        : fragmentForBlock.end();
      if (target == fragmentForBlock.end() ||
          !hasEdge(fragment.getId(), target->second,
                   sim::ComputeEdgeKind::Spawn)) {
        design.emitOpError("compute graph omits a spawn edge");
        signalPassFailure();
        return;
      }
    }
  }

  llvm::SmallDenseSet<uint32_t> scheduled;
  SmallVector<unsigned> groupForNode(nodes.size(),
                                     std::numeric_limits<unsigned>::max());
  unsigned globalGroup = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      llvm::SmallDenseSet<uint32_t> members;
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size() ||
            !scheduled.insert(member).second ||
            nodeRegions[member] != region.getKind()) {
          design.emitOpError("event-region group has invalid membership");
          signalPassFailure();
          return;
        }
        members.insert(member);
        groupForNode[member] = globalGroup;
      }
      bool selfEdge = llvm::any_of(typedEdges, [&](sim::ComputeEdgeAttr edge) {
        return edge.getKind() != sim::ComputeEdgeKind::Resume &&
               edge.getSource() == edge.getTarget() &&
               members.contains(edge.getSource());
      });
      if (members.size() > 1) {
        auto reachesEveryMember = [&](bool reverse) {
          llvm::SmallDenseSet<uint32_t> reached;
          SmallVector<uint32_t> worklist{*members.begin()};
          while (!worklist.empty()) {
            uint32_t current = worklist.pop_back_val();
            if (!reached.insert(current).second)
              continue;
            for (uint32_t target : reverse ? reverseAdjacency[current]
                                           : forwardAdjacency[current])
              if (members.contains(target))
                worklist.push_back(target);
          }
          return reached.size() == members.size();
        };
        if (!reachesEveryMember(false) || !reachesEveryMember(true)) {
          design.emitOpError("multi-node schedule group is not an SCC");
          signalPassFailure();
          return;
        }
      }
      bool expectedCyclic = members.size() > 1 || selfEdge;
      sim::ComputeScheduleKind expectedSchedule =
          !expectedCyclic ? sim::ComputeScheduleKind::Acyclic
          : hasProceduralControlCycle(members, typedEdges)
              ? sim::ComputeScheduleKind::ControlLoop
              : sim::ComputeScheduleKind::Convergence;
      if (group.getSchedule() != expectedSchedule) {
        design.emitOpError("schedule group has an incorrect execution mode");
        signalPassFailure();
        return;
      }
      llvm::SmallDenseSet<Attribute> expectedFeedback;
      if (expectedSchedule == sim::ComputeScheduleKind::Convergence) {
        for (sim::ComputeEdgeAttr edge : typedEdges)
          if (members.contains(edge.getSource()) &&
              members.contains(edge.getTarget()) &&
              edge.getKind() == sim::ComputeEdgeKind::Sensitivity &&
              edge.getResource())
            expectedFeedback.insert(edge.getResource());
        if (expectedFeedback.empty()) {
          design.emitOpError(
              "cyclic schedule group has no state feedback to compare");
          signalPassFailure();
          return;
        }
      }
      llvm::SmallDenseSet<Attribute> actualFeedback;
      for (Attribute effect : group.getFeedback())
        actualFeedback.insert(effect);
      if (expectedFeedback != actualFeedback) {
        design.emitOpError("schedule feedback does not match cyclic resources");
        signalPassFailure();
        return;
      }
      ++globalGroup;
    }
  }
  if (scheduled.size() != nodes.size()) {
    design.emitOpError("event-region plans do not schedule every graph node");
    signalPassFailure();
    return;
  }
  for (sim::ComputeEdgeAttr edge : typedEdges)
    if (edge.getKind() != sim::ComputeEdgeKind::Resume &&
        nodeRegions[edge.getSource()] == nodeRegions[edge.getTarget()] &&
        groupForNode[edge.getSource()] > groupForNode[edge.getTarget()]) {
      design.emitOpError("event-region groups are not topologically ordered");
      signalPassFailure();
      return;
    }

  simlowering::DescriptorProvenanceMap descriptorProvenance;
  if (failed(simlowering::verifyRecomputedComputeAnalysis(
          design, graph, &descriptorProvenance))) {
    signalPassFailure();
    return;
  }

  llvm::SmallDenseSet<Operation *> dynamicallySpawnedFunctions;
  DenseMap<Operation *, unsigned> staticSpawnCounts;
  DenseMap<Operation *, simlowering::ReexecutingBlockSet> reexecutingBlocks;
  design.walk([&](sim::SimFuncOp function) {
    reexecutingBlocks.try_emplace(function.getOperation(),
                                  simlowering::getReexecutingBlocks(function));
  });
  auto mayReexecute = [&](sim::SimFuncOp function, Block *block) {
    return reexecutingBlocks.find(function.getOperation())
        ->second.contains(block);
  };
  design.walk([&](sim::SimFuncOp function) {
    function.walk([&](sim::SimSpawnOp spawn) {
      auto target = dyn_cast_or_null<sim::SimFuncOp>(
          SymbolTable::lookupSymbolIn(design, spawn.getCalleeAttr()));
      if (!target)
        return;
      if (function.getEntryKind() != sim::EntryKind::RootInitializer ||
          mayReexecute(function, spawn->getBlock())) {
        dynamicallySpawnedFunctions.insert(target.getOperation());
        return;
      }
      if (++staticSpawnCounts[target.getOperation()] > 1)
        dynamicallySpawnedFunctions.insert(target.getOperation());
    });
  });

  llvm::SmallDenseSet<uint64_t> nbaSiteIds, timingSiteIds, eventSiteIds;
  bool invalidSite = false;
  std::string firstInvalidSite;
  auto invalidateSite = [&](StringRef reason) {
    invalidSite = true;
    if (firstInvalidSite.empty())
      firstInvalidSite = reason.str();
  };
  design.walk([&](Operation *operation) {
    if (operation == design.getOperation())
      return;
    if (simlowering::isSuspensionTerminator(operation)) {
      auto continuationSite = simlowering::getContinuationSite(operation);
      std::optional<uint64_t> continuation =
          continuationSite ? std::optional<uint64_t>(continuationSite.getId())
                           : std::nullopt;
      auto target = operation->getNumSuccessors() == 0
                        ? fragmentForBlock.end()
                        : fragmentForBlock.find(operation->getSuccessor(0));
      if (!continuation || *continuation > UINT32_MAX ||
          *continuation >= nodeKinds.size() ||
          nodeKinds[*continuation] != NodeKind::Fragment ||
          target == fragmentForBlock.end() || target->second != *continuation)
        invalidateSite("continuation target mismatch");
    }
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation)) {
      auto timing = delay.getTimingAttr();
      sim::ComputeTimingKind expectedKind =
          simlowering::isConstantTimeValue(delay.getDelay())
              ? sim::ComputeTimingKind::Calendar
              : sim::ComputeTimingKind::DeadlineSlot;
      if (!timing || !timingSiteIds.insert(timing.getId()).second ||
          timing.getKind() != expectedKind)
        invalidateSite("suspension timing mismatch");
    }
    if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
      auto siteAttr = nba.getSiteAttr();
      if (!siteAttr) {
        invalidateSite("missing NBA site");
        return;
      }
      uint64_t site = siteAttr.getId();
      uint64_t commit = siteAttr.getCommit();
      auto function = operation->getParentOfType<sim::SimFuncOp>();
      if (!function) {
        invalidateSite("NBA outside a function");
        return;
      }
      simlowering::DescriptorProvenance destination =
          getProvenance(nba.getDestination(), descriptorProvenance);
      bool expectedFixed =
          function.getEntryKind() != sim::EntryKind::Function &&
          !dynamicallySpawnedFunctions.contains(function.getOperation()) &&
          !mayReexecute(function, operation->getBlock());
      sim::ComputeNBAStorageKind expectedStorage =
          expectedFixed ? sim::ComputeNBAStorageKind::FixedSlot
          : !nba.getDelay() && destination.descriptor &&
                  graph.getVpi() != sim::ComputeVPIMode::Full
              ? sim::ComputeNBAStorageKind::RootAccumulator
              : sim::ComputeNBAStorageKind::DynamicFrontier;
      bool inventoryMatch = commit < nodeKinds.size() &&
                            nodeKinds[commit] == NodeKind::NBACommit &&
                            siteAttr.getStorage() == expectedStorage;
      auto source = fragmentForBlock.find(operation->getBlock());
      sim::ComputeEffectAttr nbaEffect =
          getEffect(&getContext(), sim::ComputeEffectKind::NBA, destination,
                    static_cast<bool>(nba.getDelay()));
      bool fragmentHasEffect = false;
      if (source != fragmentForBlock.end())
        for (Attribute attribute : fragmentNodes[source->second].getEffects()) {
          auto effect = cast<sim::ComputeEffectAttr>(attribute);
          if (effect == nbaEffect) {
            fragmentHasEffect = true;
            break;
          }
        }
      if (inventoryMatch) {
        auto commitNode = cast<sim::ComputeNBACommitAttr>(nodes[commit]);
        sim::ComputeEffectAttr expectedCommit =
            getEffect(&getContext(), sim::ComputeEffectKind::Write,
                      getRootProvenance(destination));
        inventoryMatch =
            commitNode.getEffect() == expectedCommit && fragmentHasEffect;
      }
      if (inventoryMatch)
        switch (siteAttr.getStorage()) {
        case sim::ComputeNBAStorageKind::FixedSlot:
          inventoryMatch = nbaSlots[commit].contains(site);
          break;
        case sim::ComputeNBAStorageKind::RootAccumulator:
          inventoryMatch = nbaAccumulatorSites[commit].contains(site);
          break;
        case sim::ComputeNBAStorageKind::StaticJournal:
          inventoryMatch = nbaStaticJournalSites[commit].contains(site);
          break;
        case sim::ComputeNBAStorageKind::DynamicFrontier:
          inventoryMatch = nbaFrontierSites[commit].contains(site);
          break;
        }
      if (inventoryMatch)
        inventoryMatch =
            source != fragmentForBlock.end() &&
            hasResourceEdge(source->second, commit,
                            sim::ComputeEdgeKind::NBAStage, nbaEffect);
      if (!nbaSiteIds.insert(site).second || !inventoryMatch)
        invalidateSite("NBA site or commit inventory mismatch");
      sim::TimingSiteAttr timing = siteAttr.getTiming();
      if (nba.getDelay()) {
        if (!timing || timing.getKind() != sim::ComputeTimingKind::DelayedNBA ||
            !timingSiteIds.insert(timing.getId()).second)
          invalidateSite("delayed NBA timing mismatch");
      } else if (timing) {
        invalidateSite("immediate NBA has a timing site");
      }
    }
    if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(operation);
        trigger && trigger.getNonblocking()) {
      auto eventSite = trigger.getSiteAttr();
      if (!eventSite) {
        invalidateSite("missing deferred-event site");
        return;
      }
      uint64_t site = eventSite.getId();
      uint64_t commit = eventSite.getCommit();
      auto source = fragmentForBlock.find(operation->getBlock());
      simlowering::DescriptorProvenance destination =
          getProvenance(trigger.getEvent(), descriptorProvenance);
      sim::ComputeEffectAttr eventEffect = getEffect(
          &getContext(), sim::ComputeEffectKind::Trigger, destination, true);
      bool fragmentHasEffect = false;
      if (source != fragmentForBlock.end())
        for (Attribute attribute : fragmentNodes[source->second].getEffects()) {
          auto effect = cast<sim::ComputeEffectAttr>(attribute);
          if (effect == eventEffect) {
            fragmentHasEffect = true;
            break;
          }
        }
      bool commitMatches = commit < nodeKinds.size() &&
                           nodeKinds[commit] == NodeKind::EventCommit &&
                           fragmentHasEffect;
      if (commitMatches) {
        sim::ComputeEffectAttr effect =
            cast<sim::ComputeEventCommitAttr>(nodes[commit]).getEffect();
        sim::ComputeEffectAttr expectedCommit =
            getEffect(&getContext(), sim::ComputeEffectKind::Trigger,
                      getRootProvenance(destination), true);
        commitMatches = effect == expectedCommit;
      }
      if (!eventSiteIds.insert(site).second || !commitMatches ||
          !eventSlots[commit].contains(site) ||
          source == fragmentForBlock.end() ||
          !hasResourceEdge(source->second, commit,
                           sim::ComputeEdgeKind::DeferredStage, eventEffect))
        invalidateSite("deferred-event site or commit inventory mismatch");
    }
  });
  auto denseSites = [](const llvm::SmallDenseSet<uint64_t> &ids) {
    for (uint64_t id = 0; id != ids.size(); ++id)
      if (!ids.contains(id))
        return false;
    return true;
  };
  if (invalidSite || duplicateInventorySite ||
      inventoriedNBASites != nbaSiteIds ||
      inventoriedEventSites != eventSiteIds || !denseSites(nbaSiteIds) ||
      !denseSites(timingSiteIds) || !denseSites(eventSiteIds)) {
    auto diagnostic = design.emitOpError(
        "has an invalid continuation, timing, NBA, or event site");
    if (!firstInvalidSite.empty())
      diagnostic << "; first failure: " << firstInvalidSite;
    signalPassFailure();
    return;
  }

  bool invalidFunction = false;
  design.walk([&](sim::SimFuncOp function) {
    bool thisFunctionInvalid = false;
    auto abi = function.getFragmentAbiAttr();
    auto summary = function.getEffectSummaryAttr();
    if (!abi || !summary ||
        failed(verifyEffectArray(design, summary, inventory, function))) {
      function.emitOpError("has an invalid fragment ABI or effect summary");
      invalidFunction = true;
      return;
    }
    llvm::SmallDenseSet<uint32_t> expected;
    for (uint32_t id : fragmentsForFunction[function.getOperation()])
      expected.insert(id);
    llvm::SmallDenseSet<uint32_t> actual;
    for (int64_t id : abi.getFragments().asArrayRef())
      if (id < 0 || static_cast<uint64_t>(id) >= nodes.size() ||
          !expected.contains(id) || !actual.insert(id).second)
        thisFunctionInvalid = true;
    size_t expectedBlocks = function.getEntryKind() == sim::EntryKind::Function
                                ? 0
                                : function.getBody().getBlocks().size();
    if (actual.size() != expected.size() || actual.size() != expectedBlocks)
      thisFunctionInvalid = true;
    for (uint32_t id : expected) {
      sim::ComputeFragmentAttr fragment = fragmentNodes[id];
      Block *block =
          &*std::next(function.getBody().begin(), fragment.getBlock());
      if (fragment.getAction() !=
          simlowering::getFragmentActionKind(block->getTerminator()))
        thisFunctionInvalid = true;
    }
    if (thisFunctionInvalid) {
      function.emitOpError("fragment ABI does not match its CFG blocks");
      invalidFunction = true;
    }
  });
  if (invalidFunction) {
    signalPassFailure();
    return;
  }
}

} // namespace
} // namespace obelisk
