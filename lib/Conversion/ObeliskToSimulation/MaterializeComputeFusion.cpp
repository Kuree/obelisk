//===- MaterializeComputeFusion.cpp - Fuse static process bodies ----------===//

#include "ComputeFusion.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
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
  Statistic eliminatedTerminationPolls{
      this, "eliminated-termination-polls",
      "redundant post-inline termination polls removed from fused bodies"};
  Statistic ifConvertedNBAs{
      this, "if-converted-nbas",
      "conditional last-write NBA diamonds converted to selects"};
  Statistic sharedStableConditions{
      this, "shared-stable-conditions",
      "equivalent stable branch conditions shared across fused actors"};
  Statistic promotedPrivateStores{
      this, "promoted-private-stores",
      "private static temporaries promoted to fused-activation SSA"};
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

/// Fold
///
///   enqueue %first to %destination
///   cond_br %condition, ^overwrite, ^continue
/// ^overwrite:
///   %second = <speculatable computation>
///   enqueue %second to %destination
///   br ^continue
///
/// to one unconditional enqueue of `select %condition, %second, %first`.
///
/// NBA values are not observable until the region barrier, so the two writes
/// have ordinary last-write semantics. Restrict this to adjacent accumulator
/// sites for the same commit root and to a speculatable, side-effect-free
/// overwrite arm. Besides removing a hot branch, the resulting straight-line
/// arithmetic is suitable for downstream SLP/vector formation.
uint64_t ifConvertConditionalNBAWrites(sim::SimFuncOp function,
                                       Block *protectedWait) {
  uint64_t converted = 0;
  bool changed;
  do {
    changed = false;
    for (Block &source : function.getBody()) {
      auto conditional = dyn_cast<cf::CondBranchOp>(source.getTerminator());
      if (!conditional || !conditional.getTrueDestOperands().empty() ||
          !conditional.getFalseDestOperands().empty())
        continue;
      Block *overwrite = conditional.getTrueDest();
      Block *continuation = conditional.getFalseDest();
      if (overwrite == continuation || overwrite == protectedWait ||
          !llvm::hasSingleElement(overwrite->getPredecessors()) ||
          overwrite->getNumArguments() != 0)
        continue;
      auto join = dyn_cast<cf::BranchOp>(overwrite->getTerminator());
      if (!join || join.getDest() != continuation ||
          !join.getDestOperands().empty())
        continue;

      sim::SimNBAEnqueueOp first;
      for (Operation &operation : llvm::reverse(source.without_terminator())) {
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          first = enqueue;
          break;
        }
      }
      if (!first || first.getDelay())
        continue;
      bool safeTail = true;
      for (Operation *operation = first->getNextNode();
           operation && operation != source.getTerminator();
           operation = operation->getNextNode())
        safeTail &=
            isa<sim::SimRefLoadOp>(operation) ||
            (isMemoryEffectFree(operation) && isSpeculatable(operation));
      if (!safeTail)
        continue;

      sim::SimNBAEnqueueOp second;
      bool safeOverwrite = true;
      for (Operation &operation : overwrite->without_terminator()) {
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          if (second)
            safeOverwrite = false;
          second = enqueue;
          continue;
        }
        safeOverwrite &=
            isMemoryEffectFree(&operation) && isSpeculatable(&operation);
      }
      if (!safeOverwrite || !second || second.getDelay() ||
          first.getDestination() != second.getDestination() ||
          first.getValue().getType() != second.getValue().getType())
        continue;
      sim::NBASiteAttr firstSite = first.getSiteAttr();
      sim::NBASiteAttr secondSite = second.getSiteAttr();
      if (!firstSite || !secondSite || firstSite.getTiming() ||
          secondSite.getTiming() ||
          firstSite.getStorage() !=
              sim::ComputeNBAStorageKind::RootAccumulator ||
          secondSite.getStorage() !=
              sim::ComputeNBAStorageKind::RootAccumulator ||
          firstSite.getCommit() != secondSite.getCommit())
        continue;

      // Move only the proven-speculatable value computation. The replacement
      // enqueue retains the later site's identity, which is the observable
      // last-write position in the static NBA plan.
      for (Operation &operation :
           llvm::make_early_inc_range(overwrite->without_terminator()))
        if (&operation != second.getOperation())
          operation.moveBefore(conditional);
      OpBuilder builder(conditional);
      Value selected = arith::SelectOp::create(
          builder, conditional.getLoc(), conditional.getCondition(),
          second.getValue(), first.getValue());
      sim::SimNBAEnqueueOp::create(builder, second.getLoc(), selected,
                                   second.getDestination(), Value{},
                                   secondSite);
      first.erase();
      second.erase();
      cf::BranchOp::create(builder, conditional.getLoc(), continuation);
      conditional.erase();
      overwrite->erase();

      // Join the now-single-predecessor continuation locally. Running the
      // generic canonicalizer here would CSE rematerialized constants across
      // the coroutine suspension and incorrectly force non-frameable values
      // into the process frame.
      if (continuation != protectedWait &&
          llvm::hasSingleElement(continuation->getPredecessors()) &&
          continuation->getNumArguments() == 0) {
        cast<cf::BranchOp>(source.getTerminator()).erase();
        source.getOperations().splice(source.end(),
                                      continuation->getOperations());
        continuation->erase();
      }
      ++converted;
      changed = true;
      break;
    }
  } while (changed);
  return converted;
}

std::optional<uint64_t> resolveStorageRoot(Value value) {
  llvm::SmallDenseSet<Value, 8> visited;
  while (value && visited.insert(value).second) {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      if (!function || argument.getOwner() != &function.getBody().front())
        return std::nullopt;
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::descriptorId);
      if (!descriptor || descriptor.getValue().isNegative() ||
          descriptor.getValue().getBitWidth() > 64)
        return std::nullopt;
      return descriptor.getValue().getZExtValue();
    }
    Operation *definition = value.getDefiningOp();
    if (auto view = dyn_cast_or_null<sim::SimRefExtractOp>(definition))
      value = view.getInput();
    else if (auto view = dyn_cast_or_null<sim::SimRefDynExtractOp>(definition))
      value = view.getInput();
    else if (auto view = dyn_cast_or_null<sim::SimRefSubelementOp>(definition))
      value = view.getInput();
    else if (auto view =
                 dyn_cast_or_null<sim::SimRefArrayElementOp>(definition))
      value = view.getInput();
    else
      return std::nullopt;
  }
  return std::nullopt;
}

/// Promote a static procedure temporary when this fused function is its sole
/// executable accessor and one store dominates every read. Such a declaration
/// is state only because its source-level lifetime spans activations; if every
/// activation overwrites it before use, retaining the canonical store would
/// add a signal-transition publication with no observer or semantic consumer.
uint64_t promotePrivateStaticTemporaries(sim::SimDesignOp design,
                                         sim::SimFuncOp function) {
  DenseMap<uint64_t, sim::SimStorageDeclOp> declarations;
  for (sim::SimStorageDeclOp declaration :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    declarations.try_emplace(declaration.getId(), declaration);

  DenseMap<uint64_t, SmallVector<sim::SimRefLoadOp>> loads;
  DenseMap<uint64_t, SmallVector<sim::SimRefStoreOp>> stores;
  llvm::SmallDenseSet<uint64_t, 8> accessedElsewhere;
  llvm::SmallDenseSet<uint64_t, 8> unsupportedUses;
  design.walk([&](Operation *operation) {
    Value reference;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      reference = load.getReference();
    else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
      reference = store.getReference();
    else {
      // Reference views are checked through their eventual users below, and
      // the root initializer must pass each reference to the replacement
      // fused process. Any other reference-consuming operation can observe
      // identity or state (for example an NBA enqueue, force, or foreign
      // call), so conservatively exclude its root from promotion.
      if (isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
              sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation))
        return;
      if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation);
          spawn && spawn.getCalleeAttr() == function.getSymNameAttr())
        return;
      for (Value operand : operation->getOperands()) {
        if (!isa<sim::RefType>(operand.getType()))
          continue;
        if (std::optional<uint64_t> root = resolveStorageRoot(operand))
          unsupportedUses.insert(*root);
      }
      return;
    }
    std::optional<uint64_t> root = resolveStorageRoot(reference);
    if (!root)
      return;
    if (operation->getParentOfType<sim::SimFuncOp>() != function) {
      accessedElsewhere.insert(*root);
      return;
    }
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      loads[*root].push_back(load);
    else
      stores[*root].push_back(cast<sim::SimRefStoreOp>(operation));
  });

  DominanceInfo dominance(function);
  uint64_t promoted = 0;
  for (auto &[descriptor, rootStores] : stores) {
    auto declaration = declarations.find(descriptor);
    auto rootLoads = loads.find(descriptor);
    if (declaration == declarations.end() || rootLoads == loads.end() ||
        rootStores.size() != 1 || rootLoads->second.empty() ||
        accessedElsewhere.contains(descriptor) ||
        unsupportedUses.contains(descriptor) ||
        declaration->second.getLifetime() != sim::Lifetime::Static ||
        declaration->second.getObservability() !=
            sim::ComputeObservabilityKind::Invisible)
      continue;
    sim::SimRefStoreOp store = rootStores.front();
    Value rootReference = store.getReference();
    std::optional<unsigned> totalWidth =
        sim::getPackedWidth(store.getValue().getType());
    if (!totalWidth || *totalWidth == 0 || *totalWidth > 64)
      continue;
    // The slicing sequence below operates on one integer plane. Four-state
    // values have distinct value/unknown planes and require a plane-aware
    // implementation rather than integer shifts and truncations.
    Type packedScalar = sim::getPackedScalarType(store.getValue().getType());
    if (!isa<IntegerType>(packedScalar))
      continue;

    // Accept only a tree of static subelement views, loads, and the one
    // dominating store. This excludes escapes, NBA destinations, dynamic
    // indexing, and any use whose identity could be observed elsewhere.
    llvm::SetVector<Value> family;
    family.insert(rootReference);
    bool closed = true;
    for (size_t index = 0; index < family.size() && closed; ++index) {
      for (OpOperand &use : family[index].getUses()) {
        Operation *user = use.getOwner();
        if (auto view = dyn_cast<sim::SimRefSubelementOp>(user)) {
          family.insert(view.getResult());
          continue;
        }
        if (isa<sim::SimRefLoadOp>(user) || user == store.getOperation())
          continue;
        closed = false;
        break;
      }
    }
    if (!closed ||
        !llvm::all_of(rootLoads->second, [&](sim::SimRefLoadOp load) {
          return dominance.dominates(store.getOperation(),
                                     load.getOperation()) &&
                 family.contains(load.getReference());
        }))
      continue;

    auto getPackedOffset =
        [&](Value reference) -> std::optional<std::pair<uint64_t, Type>> {
      SmallVector<sim::SimRefSubelementOp> path;
      Value current = reference;
      while (current != rootReference) {
        auto view = current.getDefiningOp<sim::SimRefSubelementOp>();
        if (!view)
          return std::nullopt;
        path.push_back(view);
        current = view.getInput();
      }
      uint64_t offset = 0;
      Type type = store.getValue().getType();
      for (sim::SimRefSubelementOp view : llvm::reverse(path)) {
        for (int64_t index : view.getIndices()) {
          if (index < 0)
            return std::nullopt;
          auto child = sim::getAggregateProvenanceSubelement(
              type, static_cast<unsigned>(index));
          if (!child ||
              child->first > std::numeric_limits<uint64_t>::max() - offset)
            return std::nullopt;
          offset += child->first;
          type =
              sim::getAggregateElementType(type, static_cast<unsigned>(index));
        }
      }
      return std::pair{offset, type};
    };

    SmallVector<std::pair<sim::SimRefLoadOp, std::pair<uint64_t, Type>>>
        replacements;
    bool representable = true;
    for (sim::SimRefLoadOp load : rootLoads->second) {
      auto selected = getPackedOffset(load.getReference());
      if (!selected || selected->second != load.getResult().getType()) {
        representable = false;
        break;
      }
      std::optional<unsigned> width = sim::getPackedWidth(selected->second);
      if (!width || selected->first > *totalWidth ||
          *width > *totalWidth - selected->first) {
        representable = false;
        break;
      }
      replacements.push_back({load, *selected});
    }
    if (!representable)
      continue;

    IntegerType flattenedType = cast<IntegerType>(packedScalar);
    OpBuilder storeBuilder(store);
    Value flattened = sim::SimPackedFlattenOp::create(
        storeBuilder, store.getLoc(), flattenedType, store.getValue());
    for (auto &[load, selected] : replacements) {
      if (selected.first == 0 &&
          selected.second == store.getValue().getType()) {
        load.getResult().replaceAllUsesWith(store.getValue());
        load.erase();
        continue;
      }
      OpBuilder builder(load);
      Value bits = flattened;
      if (selected.first != 0)
        bits = arith::ShRUIOp::create(
            builder, load.getLoc(), bits,
            arith::ConstantOp::create(
                builder, load.getLoc(), flattenedType,
                builder.getIntegerAttr(flattenedType, selected.first)));
      unsigned selectedWidth = *sim::getPackedWidth(selected.second);
      if (selectedWidth != *totalWidth)
        bits = arith::TruncIOp::create(
            builder, load.getLoc(),
            IntegerType::get(function.getContext(), selectedWidth), bits);
      Value replacement =
          isa<IntegerType>(selected.second)
              ? bits
              : sim::SimPackedUnflattenOp::create(builder, load.getLoc(),
                                                  selected.second, bits)
                    .getResult();
      load.getResult().replaceAllUsesWith(replacement);
      load.erase();
    }
    store.erase();
    for (Value reference : llvm::reverse(family))
      if (Operation *definition = reference.getDefiningOp();
          definition && definition->use_empty())
        definition->erase();
    if (flattened.use_empty())
      flattened.getDefiningOp()->erase();
    ++promoted;
  }
  return promoted;
}

/// Share branch conditions whose complete expression trees are structurally
/// identical and read only storage roots that this fused activation cannot
/// update immediately. NBA enqueues do not modify canonical state until the
/// barrier, so they do not invalidate such a condition.
uint64_t shareStableBranchConditions(sim::SimFuncOp function,
                                     Block *bodyEntry) {
  // A remaining call can mutate a captured root even when the fused body has
  // no direct store. Avoid hoisting loads across calls until interprocedural
  // mod/ref information is available here.
  bool hasCalls = false;
  function.walk([&](sim::SimCallOp) { hasCalls = true; });
  if (hasCalls)
    return 0;

  llvm::SmallDenseSet<uint64_t, 8> writtenRoots;
  function.walk([&](sim::SimRefStoreOp store) {
    if (std::optional<uint64_t> root = resolveStorageRoot(store.getReference()))
      writtenRoots.insert(*root);
  });

  llvm::DenseMap<Value, bool> stableCache;
  std::function<bool(Value)> isStable = [&](Value value) {
    if (isa<BlockArgument>(value))
      return true;
    if (auto cached = stableCache.find(value); cached != stableCache.end())
      return cached->second;
    Operation *definition = value.getDefiningOp();
    bool stable = false;
    if (auto load = dyn_cast_or_null<sim::SimRefLoadOp>(definition)) {
      std::optional<uint64_t> root = resolveStorageRoot(load.getReference());
      stable = root && !writtenRoots.contains(*root);
    } else if (definition && definition->getNumRegions() == 0 &&
               definition->getNumResults() == 1 &&
               isMemoryEffectFree(definition) && isSpeculatable(definition)) {
      stable = llvm::all_of(definition->getOperands(), isStable);
    }
    stableCache[value] = stable;
    return stable;
  };

  using ValuePair = std::pair<Value, Value>;
  llvm::DenseMap<ValuePair, bool> equivalentCache;
  std::function<bool(Value, Value)> equivalent = [&](Value lhs, Value rhs) {
    if (lhs == rhs)
      return true;
    ValuePair pair{lhs, rhs};
    if (auto cached = equivalentCache.find(pair);
        cached != equivalentCache.end())
      return cached->second;
    Operation *left = lhs.getDefiningOp();
    Operation *right = rhs.getDefiningOp();
    bool same = left && right && left->getName() == right->getName() &&
                left->getAttrs() == right->getAttrs() &&
                left->getResultTypes() == right->getResultTypes() &&
                left->getNumOperands() == right->getNumOperands();
    if (same) {
      if (auto leftLoad = dyn_cast<sim::SimRefLoadOp>(left)) {
        auto rightLoad = cast<sim::SimRefLoadOp>(right);
        same = leftLoad.getReference() == rightLoad.getReference();
      } else {
        for (auto [leftOperand, rightOperand] :
             llvm::zip_equal(left->getOperands(), right->getOperands()))
          same &= equivalent(leftOperand, rightOperand);
      }
    }
    equivalentCache[pair] = same;
    return same;
  };

  SmallVector<cf::CondBranchOp> branches;
  function.walk([&](cf::CondBranchOp branch) {
    if (isStable(branch.getCondition()))
      branches.push_back(branch);
  });
  SmallVector<SmallVector<cf::CondBranchOp>> groups;
  for (cf::CondBranchOp branch : branches) {
    auto group = llvm::find_if(groups, [&](auto &candidate) {
      return equivalent(candidate.front().getCondition(),
                        branch.getCondition());
    });
    if (group == groups.end())
      groups.push_back({branch});
    else
      group->push_back(branch);
  }

  uint64_t shared = 0;
  OpBuilder builder = OpBuilder::atBlockBegin(bodyEntry);
  for (auto &group : groups) {
    if (group.size() < 2)
      continue;
    IRMapping mapping;
    std::function<Value(Value)> cloneTree = [&](Value value) -> Value {
      if (isa<BlockArgument>(value))
        return value;
      if (Value mapped = mapping.lookupOrNull(value))
        return mapped;
      Operation *definition = value.getDefiningOp();
      for (Value operand : definition->getOperands())
        (void)cloneTree(operand);
      Operation *cloned = builder.clone(*definition, mapping);
      return cloned->getResult(0);
    };
    Value common = cloneTree(group.front().getCondition());
    for (cf::CondBranchOp branch : group)
      branch.getConditionMutable().assign(common);
    shared += group.size() - 1;
  }
  return shared;
}

FailureOr<sim::SimFuncOp> materializeFusion(
    sim::SimDesignOp design, sim::ComputeFusionAttr fusion,
    sim::ComputeGraphAttr graph,
    const DenseMap<uint32_t, uint32_t> &scheduleOrder,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee,
    uint64_t &eliminatedTerminationPolls, uint64_t &ifConvertedNBAs,
    uint64_t &sharedStableConditions, uint64_t &promotedPrivateStores) {
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

  // LowerUnit inserts a termination poll after every direct function call.
  // Inlining intentionally leaves that control boundary behind because a
  // general callee may request termination. Compute-body fusion has a stronger
  // closed-world proof: every operation in every transitive callee was checked
  // by isComputeBodyFusionEligible, which excludes finish, stop, fatal, task
  // calls, and every other scheduler-writing operation. The scheduler also
  // never starts ordinary Active work after a pre-existing termination
  // request. Consequently these post-inline polls are invariantly false for
  // the duration of the fused activation.
  //
  // Remove the return diamonds here, before rebuilding the compute graph. In
  // addition to avoiding runtime scheduler reads, this joins the arithmetic
  // into larger basic blocks that downstream scalar and vector optimizers can
  // analyze together.
  SmallVector<cf::CondBranchOp> redundantPolls;
  fused.walk([&](cf::CondBranchOp branch) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    if (!requested ||
        !isa<sim::SimReturnOp>(branch.getTrueDest()->getTerminator()))
      return;
    redundantPolls.push_back(branch);
  });
  for (cf::CondBranchOp branch : redundantPolls) {
    sim::SimTerminationRequestedOp requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    Block *source = branch->getBlock();
    Block *continuation = branch.getFalseDest();
    bool canMerge = llvm::hasSingleElement(continuation->getPredecessors()) &&
                    continuation->getNumArguments() == 0 &&
                    branch.getFalseDestOperands().empty();
    branch.erase();
    if (requested->use_empty())
      requested.erase();
    if (canMerge) {
      source->getOperations().splice(source->end(),
                                     continuation->getOperations());
      continuation->erase();
    } else {
      builder.setInsertionPointToEnd(source);
      cf::BranchOp::create(builder, fused.getLoc(), continuation, ValueRange{});
    }
    ++eliminatedTerminationPolls;
  }

  builder.setInsertionPoint(insertionSpawn);
  sim::SimSpawnOp::create(builder, fused.getLoc(), fused.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (BodyFusionCandidate &candidate : candidates)
    candidate.spawn.erase();
  for (BodyFusionCandidate &candidate : candidates)
    candidate.function.erase();

  // Remove private activation temporaries before if-converting NBA diamonds.
  // Besides avoiding canonical state publication, this turns overwrite-arm
  // loads into SSA values so only genuinely speculatable arithmetic is moved
  // out of the branch.
  promotedPrivateStores += promotePrivateStaticTemporaries(design, fused);
  ifConvertedNBAs += ifConvertConditionalNBAWrites(fused, wait);
  sharedStableConditions += shareStableBranchConditions(
      fused, clonedBlocks.front().lookup(candidates.front().body));

  // The true arms above are now unreachable single-return blocks. Erase only
  // blocks with no predecessors; any unexpected structure remains intact and
  // will be validated by the rebuilt graph.
  for (auto block = fused.getBody().begin(), end = fused.getBody().end();
       block != end;) {
    Block &current = *block++;
    if (&current != &entry && &current != wait && current.hasNoPredecessors() &&
        current.without_terminator().empty() &&
        isa<sim::SimReturnOp>(current.getTerminator()))
      current.erase();
  }

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
  uint64_t removedPolls = 0;
  uint64_t convertedNBAs = 0;
  uint64_t sharedConditions = 0;
  uint64_t promotedStores = 0;
  for (Attribute attribute : fusions) {
    auto fusion = dyn_cast<sim::ComputeFusionAttr>(attribute);
    if (!fusion)
      continue;
    FailureOr<sim::SimFuncOp> fused = materializeFusion(
        design, fusion, graph, scheduleOrder, spawnsByCallee, removedPolls,
        convertedNBAs, sharedConditions, promotedStores);
    changed |= succeeded(fused);
    if (succeeded(fused))
      ++materializedFusions;
    else
      ++rejectedFusions;
  }
  eliminatedTerminationPolls += removedPolls;
  ifConvertedNBAs += convertedNBAs;
  sharedStableConditions += sharedConditions;
  promotedPrivateStores += promotedStores;
  design->removeAttr(sim::metadata::staticBodyFusion);
  if (!changed)
    return;
  design->removeAttr(
      sim::SimDesignOp::getComputeGraphAttrName(design->getName()));
}

} // namespace
} // namespace obelisk
