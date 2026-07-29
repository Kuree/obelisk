//===- MaterializeComputeFusion.cpp - Fuse static process bodies ----------===//

#include "ComputeFusion.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZECOMPUTEFUSIONPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimMaterializeComputeFusionPass final
    : public impl::ObeliskSimMaterializeComputeFusionPassBase<
          ObeliskSimMaterializeComputeFusionPass> {
public:
  void runOnOperation() override;

private:
  Statistic materializedFusions{this, "materialized-fusions",
                                "verified process-body fusions materialized"};
  Statistic rejectedFusions{
      this, "rejected-fusions",
      "planned fusions rejected by executable-structure validation"};
};

struct BodyFusionCandidate {
  sim::SimFuncOp function;
  sim::SimSpawnOp spawn;
  Block *wait = nullptr;
  Block *body = nullptr;
  uint32_t resumeTarget = UINT32_MAX;
  uint32_t resumeOrder = UINT32_MAX;
  uint32_t entryOrder = UINT32_MAX;
  SmallVector<Operation *> entryPreamble;
  SmallVector<Block *> bodyBlocks;
  SmallVector<unsigned> fusedArguments;
};

bool isSupportedEntryKind(sim::EntryKind kind) {
  return kind == sim::EntryKind::Always || kind == sim::EntryKind::AlwaysFF;
}

bool isTypedDirectWait(Operation *operation) {
  return isa<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp>(operation);
}

bool isTypedSuspend(Operation *operation) {
  return isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
             sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
             sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
             sim::SimSuspendEventOp, sim::SimSuspendObserveOp,
             sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp>(operation);
}

bool hasOnlyPureEntryPreamble(sim::SimFuncOp function, Block *wait) {
  Block &entry = function.getBody().front();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!branch || branch.getDest() != wait || !branch.getDestOperands().empty())
    return false;
  return llvm::all_of(entry.without_terminator(), [](Operation &operation) {
    return isMemoryEffectFree(&operation);
  });
}

bool collectBodyBlocks(BodyFusionCandidate &candidate) {
  SmallVector<Block *> pending{candidate.body};
  llvm::SmallPtrSet<Block *, 16> visited;
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == candidate.wait || !visited.insert(block).second)
      continue;
    if (block == &candidate.function.getBody().front() ||
        block->getNumArguments() != 0)
      return false;
    candidate.bodyBlocks.push_back(block);
    Operation *terminator = block->getTerminator();
    if (!isa<BranchOpInterface, sim::SimReturnOp>(terminator))
      return false;
    for (Block *successor : terminator->getSuccessors()) {
      if (successor == &candidate.function.getBody().front())
        return false;
      if (successor != candidate.wait)
        pending.push_back(successor);
    }
  }
  if (candidate.bodyBlocks.size() + 2 !=
      candidate.function.getBody().getBlocks().size())
    return false;
  llvm::sort(candidate.bodyBlocks, [&](Block *lhs, Block *rhs) {
    return std::distance(candidate.function.getBody().begin(),
                         Region::iterator(lhs)) <
           std::distance(candidate.function.getBody().begin(),
                         Region::iterator(rhs));
  });
  return true;
}

void collectLiveEntryPreamble(BodyFusionCandidate &candidate) {
  Block &entry = candidate.function.getBody().front();
  llvm::SmallPtrSet<Operation *, 16> needed;
  SmallVector<Value> pending;
  pending.append(candidate.wait->getTerminator()->operand_begin(),
                 candidate.wait->getTerminator()->operand_end());
  for (Block *block : candidate.bodyBlocks)
    for (Operation &operation : *block)
      pending.append(operation.operand_begin(), operation.operand_end());
  while (!pending.empty()) {
    Operation *definition = pending.pop_back_val().getDefiningOp();
    if (!definition || definition->getBlock() != &entry ||
        definition == entry.getTerminator() ||
        !needed.insert(definition).second)
      continue;
    pending.append(definition->operand_begin(), definition->operand_end());
  }
  for (Operation &operation : entry.without_terminator())
    if (needed.contains(&operation))
      candidate.entryPreamble.push_back(&operation);
}

bool hasOnlyTerminationReturns(const BodyFusionCandidate &candidate) {
  for (Block *block : candidate.bodyBlocks) {
    if (!isa<sim::SimReturnOp>(block->getTerminator()))
      continue;
    bool hasPredecessor = false;
    for (Block *predecessor : block->getPredecessors()) {
      hasPredecessor = true;
      auto branch = dyn_cast<cf::CondBranchOp>(predecessor->getTerminator());
      if (!branch || branch.getTrueDest() != block ||
          !branch.getCondition()
               .getDefiningOp<sim::SimTerminationRequestedOp>())
        return false;
    }
    if (!hasPredecessor)
      return false;
  }
  return true;
}

sim::ComputeEffectAttr getDirectSensitivity(sim::ComputeFragmentAttr fragment) {
  sim::ComputeEffectAttr sensitivity;
  for (Attribute attribute : fragment.getEffects()) {
    auto effect = cast<sim::ComputeEffectAttr>(attribute);
    if (effect.getEffect() != sim::ComputeEffectKind::Watch)
      continue;
    if (sensitivity)
      return {};
    sensitivity = effect;
  }
  return sensitivity;
}

FailureOr<sim::SimFuncOp> materializeFusion(
    sim::SimDesignOp design, sim::ComputeFusionAttr fusion,
    sim::ComputeGraphAttr graph,
    const DenseMap<uint32_t, uint32_t> &scheduleOrder,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee) {
  DenseMap<uint32_t, uint32_t> resumeTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Resume)
      resumeTargets[edge.getSource()] = edge.getTarget();
  }
  SmallVector<uint32_t> entryTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Spawn &&
        scheduleOrder.contains(edge.getTarget()))
      entryTargets.push_back(edge.getTarget());
  }
  llvm::sort(entryTargets, [&](uint32_t lhs, uint32_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  entryTargets.erase(std::unique(entryTargets.begin(), entryTargets.end()),
                     entryTargets.end());
  DenseMap<StringAttr, uint32_t> entryOrder;
  for (auto [order, target] : llvm::enumerate(entryTargets)) {
    auto fragment =
        target < graph.getNodes().size()
            ? dyn_cast<sim::ComputeFragmentAttr>(graph.getNodes()[target])
            : sim::ComputeFragmentAttr{};
    if (fragment)
      entryOrder.try_emplace(fragment.getFunction().getAttr(),
                             static_cast<uint32_t>(order));
  }

  SmallVector<BodyFusionCandidate> candidates;
  sim::ComputeEffectAttr commonSensitivity;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return failure();
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
        graph.getNodes()[static_cast<size_t>(member)]);
    if (!fragment)
      return failure();
    sim::ComputeEffectAttr sensitivity = getDirectSensitivity(fragment);
    if (!sensitivity || (commonSensitivity && sensitivity != commonSensitivity))
      return failure();
    commonSensitivity = sensitivity;
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    auto spawns = function ? spawnsByCallee.find(function.getSymNameAttr())
                           : spawnsByCallee.end();
    auto resume = resumeTargets.find(static_cast<uint32_t>(member));
    sim::SimFuncOp spawningFunction =
        function && spawns != spawnsByCallee.end() && spawns->second.size() == 1
            ? spawns->second.front()->getParentOfType<sim::SimFuncOp>()
            : sim::SimFuncOp{};
    if (!function || !isSupportedEntryKind(function.getEntryKind()) ||
        !isComputeBodyFusionEligible(function) ||
        spawns == spawnsByCallee.end() || spawns->second.size() != 1 ||
        !spawningFunction ||
        spawningFunction.getEntryKind() != sim::EntryKind::RootInitializer ||
        !spawns->second.front()->getResult(0).use_empty() ||
        resume == resumeTargets.end())
      return failure();

    Block *wait = nullptr;
    uint32_t blockIndex = 0;
    for (Block &block : function.getBody()) {
      if (blockIndex++ == fragment.getBlock()) {
        wait = &block;
        break;
      }
    }
    if (!wait || !wait->without_terminator().empty() ||
        wait->getNumArguments() != 0 ||
        !isTypedDirectWait(wait->getTerminator()) ||
        wait->getNumSuccessors() != 1 ||
        wait->getSuccessor(0)->getNumArguments() != 0 ||
        !hasOnlyPureEntryPreamble(function, wait))
      return failure();
    unsigned suspensionCount = 0;
    function.walk([&](Operation *operation) {
      suspensionCount += isTypedSuspend(operation);
    });
    if (suspensionCount != 1)
      return failure();

    BodyFusionCandidate candidate;
    candidate.function = function;
    candidate.spawn = spawns->second.front();
    candidate.wait = wait;
    candidate.body = wait->getSuccessor(0);
    candidate.resumeTarget = resume->second;
    auto functionEntry = entryOrder.find(function.getSymNameAttr());
    if (functionEntry == entryOrder.end())
      return failure();
    candidate.entryOrder = functionEntry->second;
    if (!collectBodyBlocks(candidate) || !hasOnlyTerminationReturns(candidate))
      return failure();
    collectLiveEntryPreamble(candidate);
    candidates.push_back(std::move(candidate));
  }
  if (candidates.size() < 2)
    return failure();
  SmallVector<uint32_t> readyTargets =
      getComputeFusionReadyTargets(graph, commonSensitivity);
  llvm::erase_if(readyTargets, [&](uint32_t target) {
    return !scheduleOrder.contains(target);
  });
  llvm::sort(readyTargets, [&](uint32_t lhs, uint32_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  readyTargets.erase(std::unique(readyTargets.begin(), readyTargets.end()),
                     readyTargets.end());
  DenseMap<uint32_t, uint32_t> readyOrder;
  for (auto [order, target] : llvm::enumerate(readyTargets))
    readyOrder.try_emplace(target, static_cast<uint32_t>(order));
  for (BodyFusionCandidate &candidate : candidates) {
    auto order = readyOrder.find(candidate.resumeTarget);
    if (order == readyOrder.end())
      return failure();
    candidate.resumeOrder = order->second;
  }
  llvm::sort(candidates, [](const auto &lhs, const auto &rhs) {
    return lhs.resumeOrder < rhs.resumeOrder;
  });

  // A body that publishes an Active-region sensitivity can make another actor
  // runnable between two members. Keep those processes separate until the
  // graph proves that no fused body can introduce such an activation.
  llvm::SmallDenseSet<uint32_t> candidateFragments;
  llvm::SmallDenseSet<StringAttr> candidateFunctions;
  for (BodyFusionCandidate &candidate : candidates)
    candidateFunctions.insert(candidate.function.getSymNameAttr());
  for (auto [index, attribute] : llvm::enumerate(graph.getNodes())) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment ||
        !candidateFunctions.contains(fragment.getFunction().getAttr()))
      continue;
    if (fragment.getTier() != sim::ComputeTierKind::Native)
      return failure();
    candidateFragments.insert(static_cast<uint32_t>(index));
  }
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (candidateFragments.contains(edge.getSource()) &&
        (edge.getKind() == sim::ComputeEdgeKind::Sensitivity ||
         edge.getKind() == sim::ComputeEdgeKind::Spawn))
      return failure();
  }

  // Fusing two actors makes their bodies indivisible. They must therefore be
  // adjacent in the complete deterministic Active schedule, not merely among
  // actors sharing this sensitivity: another sensitivity can become ready in
  // the same slot and occupy an intervening schedule rank.
  for (auto [index, candidate] : llvm::enumerate(candidates)) {
    if (candidate.resumeOrder !=
            static_cast<uint64_t>(candidates.front().resumeOrder) + index ||
        candidate.entryOrder !=
            static_cast<uint64_t>(candidates.front().entryOrder) + index)
      return failure();
  }

  SmallVector<Value> operands;
  SmallVector<Type> inputTypes;
  SmallVector<DictionaryAttr> argumentAttrs;
  DenseMap<Value, unsigned> operandIndices;
  sim::SimSpawnOp insertionSpawn = candidates.front().spawn;
  for (BodyFusionCandidate &candidate : candidates) {
    if (candidate.spawn->getBlock() != insertionSpawn->getBlock())
      return failure();
    if (insertionSpawn->isBeforeInBlock(candidate.spawn))
      insertionSpawn = candidate.spawn;
    Block &entry = candidate.function.getBody().front();
    if (entry.getNumArguments() != candidate.spawn.getNumOperands())
      return failure();
    for (auto [argument, operand] :
         llvm::zip_equal(entry.getArguments(), candidate.spawn.getOperands())) {
      auto [found, inserted] =
          operandIndices.try_emplace(operand, operands.size());
      unsigned index = found->second;
      DictionaryAttr attrs =
          candidate.function.getArgAttrDict(argument.getArgNumber());
      if (inserted) {
        operands.push_back(operand);
        inputTypes.push_back(argument.getType());
        argumentAttrs.push_back(attrs);
      } else if (inputTypes[index] != argument.getType() ||
                 argumentAttrs[index] != attrs) {
        return failure();
      }
      candidate.fusedArguments.push_back(index);
    }
  }

  sim::SimFuncOp first = candidates.front().function;
  unsigned symbolCounter = 0;
  SmallString<32> symbolBase;
  ("__obelisk_fused_" + Twine(fusion.getId())).toVector(symbolBase);
  SmallString<32> name = SymbolTable::generateSymbolName<32>(
      symbolBase,
      [&](StringRef candidate) {
        return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
      },
      symbolCounter);
  SmallVector<NamedAttribute> fusedAttributes;
  if (IntegerAttr codeUnit = first.getCodeUnitIdAttr())
    fusedAttributes.emplace_back(first.getCodeUnitIdAttrName(), codeUnit);
  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  sim::SimFuncOp fused = sim::SimFuncOp::create(
      builder, first.getLoc(), name,
      FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
      first.getEntryKind(), fusedAttributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(fused, SymbolTable::Visibility::Private);

  Block &entry = fused.getBody().front();
  Block *wait = new Block;
  fused.getBody().push_back(wait);
  SmallVector<std::unique_ptr<IRMapping>> mappings;
  mappings.reserve(candidates.size());
  SmallVector<DenseMap<Block *, Block *>> clonedBlocks(candidates.size());
  for (auto [candidateIndex, candidate] : llvm::enumerate(candidates)) {
    auto mapping = std::make_unique<IRMapping>();
    for (auto [argument, fusedIndex] :
         llvm::zip_equal(candidate.function.getBody().front().getArguments(),
                         candidate.fusedArguments))
      mapping->map(argument, entry.getArgument(fusedIndex));
    for (Block *source : candidate.bodyBlocks) {
      Block *cloned = new Block;
      fused.getBody().push_back(cloned);
      mapping->map(source, cloned);
      clonedBlocks[candidateIndex][source] = cloned;
    }
    mappings.push_back(std::move(mapping));
  }
  for (auto [index, candidate] : llvm::enumerate(candidates)) {
    Block *next =
        index + 1 == candidates.size()
            ? wait
            : clonedBlocks[index + 1].lookup(candidates[index + 1].body);
    mappings[index]->map(candidate.wait, next);
  }

  builder.setInsertionPointToStart(&entry);
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings))
    for (Operation *operation : candidate.entryPreamble)
      builder.clone(*operation, *mapping);
  cf::BranchOp::create(builder, fused.getLoc(), wait);

  builder.setInsertionPointToStart(wait);
  builder.clone(*candidates.front().wait->getTerminator(), *mappings.front());
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
    for (Block *source : candidate.bodyBlocks) {
      Block *destination = mapping->lookup(source);
      builder.setInsertionPointToEnd(destination);
      for (Operation &operation : *source)
        builder.clone(operation, *mapping);
    }
  }

  builder.setInsertionPoint(insertionSpawn);
  sim::SimSpawnOp::create(builder, fused.getLoc(), fused.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (BodyFusionCandidate &candidate : candidates)
    candidate.spawn.erase();
  for (BodyFusionCandidate &candidate : candidates)
    candidate.function.erase();
  return fused;
}

void ObeliskSimMaterializeComputeFusionPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  ArrayAttr fusions =
      design->getAttrOfType<ArrayAttr>(sim::metadata::staticBodyFusion);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!fusions || !graph || graph.getWorkers() != 1 ||
      graph.getVpi() != sim::ComputeVPIMode::Off)
    return;

  DenseMap<uint32_t, uint32_t> scheduleOrder;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups())
      for (int64_t member : cast<sim::ComputeGroupAttr>(groupAttribute)
                                .getFragments()
                                .asArrayRef())
        scheduleOrder[static_cast<uint32_t>(member)] = nextOrder++;
  }

  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawnsByCallee;
  design.walk([&](sim::SimSpawnOp spawn) {
    spawnsByCallee[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });

  bool changed = false;
  for (Attribute attribute : fusions) {
    auto fusion = dyn_cast<sim::ComputeFusionAttr>(attribute);
    if (!fusion)
      continue;
    FailureOr<sim::SimFuncOp> fused =
        materializeFusion(design, fusion, graph, scheduleOrder, spawnsByCallee);
    changed |= succeeded(fused);
    if (succeeded(fused))
      ++materializedFusions;
    else
      ++rejectedFusions;
  }
  design->removeAttr(sim::metadata::staticBodyFusion);
  if (!changed)
    return;
  design->removeAttr(
      sim::SimDesignOp::getComputeGraphAttrName(design->getName()));
}

} // namespace
} // namespace obelisk
