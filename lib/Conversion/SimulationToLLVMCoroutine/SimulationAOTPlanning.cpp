//===- SimulationAOTPlanning.cpp - Native AOT plan derivation -----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult
specializeNativeAOTCaptures(ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility) {
  (void)eligibility;
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    return module.emitError(
        "cannot specialize AOT captures without a root initializer");

  // Capture addressing is independent of scheduler eligibility.  A callee
  // with exactly one whole-design spawn has the same fixed context object on
  // every activation even when conditional waits or control loops keep that
  // actor on the generic scheduler.  Duplicate or dynamic spawns remain on
  // ordinary frame captures.
  llvm::StringMap<unsigned> spawnCounts;
  module.walk([&](sim::SimSpawnOp spawn) {
    ++spawnCounts[spawn.getCallee()];
  });

  WalkResult specialized = root.walk([&](sim::SimSpawnOp spawn) {
    sim::SimDesignOp design = spawn->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp target =
        design ? design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee())
               : nullptr;
    if (!target || spawnCounts.lookup(spawn.getCallee()) != 1)
      return WalkResult::advance();
    Block &entry = target.getBody().front();
    if (spawn.getNumOperands() != entry.getNumArguments()) {
      spawn.emitOpError("AOT capture specialization found an invalid arity");
      return WalkResult::interrupt();
    }
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType())) {
      target.emitOpError(
          "AOT capture specialization requires a context entry capture");
      return WalkResult::interrupt();
    }

    for (unsigned index = 1; index != entry.getNumArguments(); ++index) {
      Operation *producer = spawn.getOperand(index).getDefiningOp();
      if (!producer ||
          !isa<sim::SimContextStorageOp, sim::SimContextNetOp,
               sim::SimContextDriverOp, sim::SimContextEventOp>(producer))
        continue;
      if (producer->getNumOperands() != 1 ||
          producer->getOperand(0) != spawn.getOperand(0) ||
          producer->getNumResults() != 1 ||
          producer->getResult(0) != spawn.getOperand(index))
        continue;

      SmallVector<OpOperand *> uses;
      for (OpOperand &use : entry.getArgument(index).getUses())
        uses.push_back(&use);
      DenseMap<Block *, Value> specializedByBlock;
      for (OpOperand *use : uses) {
        Block *block = use->getOwner()->getBlock();
        auto [position, inserted] =
            specializedByBlock.try_emplace(block, Value{});
        if (inserted) {
          OpBuilder builder(target.getContext());
          builder.setInsertionPointToStart(block);
          IRMapping mapping;
          mapping.map(spawn.getOperand(0), entry.getArgument(0));
          position->second = builder.clone(*producer, mapping)->getResult(0);
        }
        use->set(position->second);
      }
    }
    return WalkResult::advance();
  });
  return specialized.wasInterrupted() ? failure() : success();
}
FailureOr<SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots) {
  SmallVector<obelisk_rt_static_actor_root> plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  auto specialization =
      design ? design->getAttrOfType<sim::StaticSpecializationAttr>(
                   sim::metadata::staticSpecialization)
             : sim::StaticSpecializationAttr{};
  if (!specialization)
    return plan;
  for (Attribute attribute : specialization.getActorRoots()) {
    auto dependency = dyn_cast<sim::StaticActorRootAttr>(attribute);
    if (!dependency)
      return module.emitError("invalid static actor/root dependency"),
             failure();
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(dependency.getFunction());
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || actor == actorSlots.end())
      continue;
    auto handle = stateLayout.storage.find(dependency.getDescriptor());
    if (handle == stateLayout.storage.end())
      return module.emitError(
                 "static actor/root dependency references unknown storage"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return module.emitError(
                 "static actor/root dependency has an invalid native handle"),
             failure();
    uint32_t flags = (dependency.getRead() ? OBELISK_RT_STATIC_ROOT_READ : 0) |
                     (dependency.getWrite() ? OBELISK_RT_STATIC_ROOT_WRITE : 0);
    if (flags != 0)
      plan.push_back({actor->second, decoded.id, flags, 0});
  }
  llvm::sort(plan, [](const auto &lhs, const auto &rhs) {
    return std::tuple{lhs.actor_slot, lhs.static_state, lhs.flags} <
           std::tuple{rhs.actor_slot, rhs.static_state, rhs.flags};
  });
  plan.erase(std::unique(plan.begin(), plan.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.actor_slot == rhs.actor_slot &&
                                  lhs.static_state == rhs.static_state &&
                                  lhs.flags == rhs.flags;
                         }),
             plan.end());
  return plan;
}

FailureOr<NativeStaticFanoutPlan> buildNativeStaticFanoutPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots, bool enabled) {
  NativeStaticFanoutPlan plan;
  plan.exact = enabled;
  if (!enabled)
    return plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  sim::ComputeGraphAttr graph = design ? design.getComputeGraphAttr() : nullptr;
  if (!graph)
    return module.emitError("static fanout plan requires a compute graph"),
           failure();
  auto disableExactFanout = [&] {
    plan.entries.clear();
    plan.exact = false;
  };
  for (Attribute node : graph.getNodes()) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    SmallVector<sim::ComputeEffectAttr> watches;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() == sim::ComputeEffectKind::Watch)
        watches.push_back(effect);
    }
    if (watches.empty())
      continue;
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    Block *block =
        function
            ? analysis::lookupComputeGraphBlock(function, fragment.getBlock())
            : nullptr;
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || !block || actor == actorSlots.end())
      return module.emitError(
                 "static fanout references a stale compute fragment"),
             failure();
    Operation *terminator = block->getTerminator();
    sim::ContinuationSiteAttr site;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(terminator))
      site = suspend.getSiteAttr();
    else {
      disableExactFanout();
      continue;
    }
    if (!site || site.getId() == 0)
      return terminator->emitError(
                 "static fanout suspension has no continuation metadata"),
             failure();
    for (sim::ComputeEffectAttr effect : watches) {
      if (effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
          effect.getDynamic() || effect.getDeferred() ||
          (effect.getResource() != sim::ComputeResourceKind::Storage &&
           effect.getResource() != sim::ComputeResourceKind::Net)) {
        disableExactFanout();
        continue;
      }
      const auto &handles =
          effect.getResource() == sim::ComputeResourceKind::Storage
              ? stateLayout.storage
              : stateLayout.nets;
      auto handle = handles.find(effect.getDescriptor());
      if (handle == handles.end())
        return terminator->emitError(
                   "static fanout references an unknown state descriptor"),
               failure();
      obelisk_rt_stable_handle_v1 decoded{};
      if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return terminator->emitError(
                   "static fanout descriptor has an invalid native root"),
               failure();
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == decoded.id;
          });
      if (bound == stateLayout.bounds.end() || effect.getWidth() == 0 ||
          effect.getLow() > bound->width ||
          effect.getWidth() > bound->width - effect.getLow())
        return terminator->emitError("static fanout range is out of bounds"),
               failure();
      uint32_t edge;
      switch (effect.getTrigger()) {
      case sim::ComputeTriggerKind::Change:
        edge = OBELISK_RT_WAIT_EDGE_CHANGE;
        break;
      case sim::ComputeTriggerKind::Posedge:
        edge = OBELISK_RT_WAIT_EDGE_POSEDGE;
        break;
      case sim::ComputeTriggerKind::Negedge:
        edge = OBELISK_RT_WAIT_EDGE_NEGEDGE;
        break;
      case sim::ComputeTriggerKind::Both:
        edge = OBELISK_RT_WAIT_EDGE_BOTH;
        break;
      default:
        disableExactFanout();
        continue;
      }
      plan.entries.push_back({decoded.id, actor->second, site.getId(), edge,
                              UINT32_MAX, 0, effect.getLow(),
                              effect.getWidth()});
    }
  }
  llvm::sort(plan.entries, [](const auto &lhs, const auto &rhs) {
    return std::tuple{lhs.static_state, lhs.low_bit, lhs.actor_slot,
                      lhs.continuation} <
           std::tuple{rhs.static_state, rhs.low_bit, rhs.actor_slot,
                      rhs.continuation};
  });
  if (std::adjacent_find(plan.entries.begin(), plan.entries.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.static_state == rhs.static_state &&
                                  lhs.actor_slot == rhs.actor_slot &&
                                  lhs.continuation == rhs.continuation &&
                                  lhs.edge == rhs.edge &&
                                  lhs.low_bit == rhs.low_bit &&
                                  lhs.bit_width == rhs.bit_width;
                         }) != plan.entries.end())
    return module.emitError("static fanout entry is duplicated"), failure();
  return plan;
}

} // namespace obelisk::detail
