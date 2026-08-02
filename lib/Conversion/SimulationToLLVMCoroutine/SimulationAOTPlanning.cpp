//===- SimulationAOTPlanning.cpp - Native AOT plan derivation -----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

FailureOr<SmallVector<NativePeriodicClock>> buildNativePeriodicClockPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots) {
  SmallVector<NativePeriodicClock> clocks;
  WalkResult result = module.walk([&](sim::SimFuncOp function) {
    auto actor = actorSlots.find(function.getOperation());
    if (actor == actorSlots.end() || function.isExternal() ||
        function.getBody().empty())
      return WalkResult::advance();

    SmallVector<sim::SimSuspendDelayOp> delays;
    SmallVector<sim::SimRefLoadOp> loads;
    SmallVector<sim::SimRefStoreOp> stores;
    SmallVector<sim::SimLogicUnaryOp> unaries;
    function.walk([&](Operation *operation) {
      if (auto op = dyn_cast<sim::SimSuspendDelayOp>(operation))
        delays.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefLoadOp>(operation))
        loads.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefStoreOp>(operation))
        stores.push_back(op);
      else if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation))
        unaries.push_back(op);
    });
    if (delays.size() != 1 || loads.size() != 1 || stores.size() != 1 ||
        unaries.size() != 1 ||
        unaries.front().getKind() != sim::UnaryKind::BitNot)
      return WalkResult::advance();

    sim::SimSuspendDelayOp delay = delays.front();
    auto period = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
    if (!period || period.getValue() == 0 || !delay.getTimingAttr() ||
        delay.getContinuationOperands().size() != 0)
      return WalkResult::advance();
    sim::SimRefLoadOp load = loads.front();
    sim::SimRefStoreOp store = stores.front();
    sim::SimLogicUnaryOp unary = unaries.front();
    if (unary.getInput() != load.getResult() ||
        store.getValue() != unary.getResult() ||
        load.getReference() != store.getReference())
      return WalkResult::advance();
    auto reference = dyn_cast<BlockArgument>(load.getReference());
    auto storage =
        load.getReference().getDefiningOp<sim::SimContextStorageOp>();
    if ((!reference || reference.getOwner() != &function.getBody().front()) &&
        !storage)
      return WalkResult::advance();
    if (storage &&
        (function.getBody().front().getNumArguments() == 0 ||
         storage.getContext() != function.getBody().front().getArgument(0)))
      return WalkResult::advance();
    auto refType = dyn_cast<sim::RefType>(load.getReference().getType());
    auto logicType = refType
                         ? dyn_cast<sim::LogicType>(refType.getElementType())
                         : sim::LogicType{};
    if (!logicType || logicType.getWidth() != 1)
      return WalkResult::advance();

    // Accept only the canonical delay -> toggle -> delay recurrence.  This is
    // intentionally stricter than merely finding the four operations: any
    // side branch, second effect, or phase-dependent delay stays in Tier 3.
    Block *wait = delay->getBlock();
    Block *toggle = delay.getContinuation();
    auto back = dyn_cast<cf::BranchOp>(toggle->getTerminator());
    if (!back || back.getDest() != wait || wait->getNumSuccessors() != 1 ||
        wait->getSuccessor(0) != toggle)
      return WalkResult::advance();
    bool unsupported = false;
    function.walk([&](Operation *operation) {
      if (operation == function.getOperation())
        return;
      if (isa<sim::SimSuspendDelayOp, sim::SimRefLoadOp, sim::SimRefStoreOp,
              sim::SimLogicUnaryOp, sim::SimTimeConstantOp,
              sim::SimContextStorageOp, cf::BranchOp>(operation))
        return;
      if (!isMemoryEffectFree(operation))
        unsupported = true;
    });
    if (unsupported)
      return WalkResult::advance();

    std::optional<uint64_t> descriptor;
    if (reference) {
      auto attribute = function.getArgAttrOfType<IntegerAttr>(
          reference.getArgNumber(), sim::metadata::descriptorId);
      if (attribute)
        descriptor = attribute.getInt();
    } else {
      descriptor = storage.getId();
    }
    if (!descriptor)
      return WalkResult::advance();
    auto offset = stateLayout.storageOffsets.find(*descriptor);
    if (offset == stateLayout.storageOffsets.end())
      return WalkResult::advance();
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.offset == offset->second && candidate.width == 1;
    });
    if (bound == stateLayout.bounds.end())
      return WalkResult::advance();
    auto site = delay.getSiteAttr();
    if (!site || site.getId() == 0)
      return delay.emitOpError("periodic clock wait has no continuation ID"),
             WalkResult::interrupt();
    clocks.push_back({actor->second, site.getId(), bound->handleID,
                      offset->second, period.getValue()});
    return WalkResult::advance();
  });
  if (result.wasInterrupted())
    return failure();
  llvm::sort(clocks, [](const NativePeriodicClock &lhs,
                        const NativePeriodicClock &rhs) {
    return std::tuple{lhs.halfPeriod, lhs.staticState, lhs.bitOffset,
                      lhs.actorSlot} < std::tuple{rhs.halfPeriod,
                                                  rhs.staticState,
                                                  rhs.bitOffset, rhs.actorSlot};
  });
  for (auto pair : llvm::zip(clocks, llvm::drop_begin(clocks))) {
    const auto &lhs = std::get<0>(pair);
    const auto &rhs = std::get<1>(pair);
    if (lhs.staticState == rhs.staticState && lhs.bitOffset == rhs.bitOffset)
      return module.emitError(
                 "multiple periodic generators drive the same physical bit"),
             failure();
  }
  return clocks;
}

LogicalResult materializeNativePeriodicClockPlan(
    ModuleOp module, ArrayRef<NativePeriodicClock> periodicClocks) {
  if (periodicClocks.empty())
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type entryType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32, i64, i64});
  Type tableType = LLVM::LLVMArrayType::get(entryType, periodicClocks.size());
  if (module.lookupSymbol("__obelisk_periodic_clock_plan_v1"))
    return module.emitError("duplicate generated periodic-clock plan");
  makeConstantGlobal(
      module, location, tableType, "__obelisk_periodic_clock_plan_v1",
      LLVM::Linkage::Internal, 8, [&](OpBuilder &initializer) {
        Value table = LLVM::ZeroOp::create(initializer, location, tableType);
        for (auto [index, clock] : llvm::enumerate(periodicClocks)) {
          Value entry = LLVM::ZeroOp::create(initializer, location, entryType);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.actorSlot), 0);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.continuation), 1);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.staticState), 2);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i64, clock.bitOffset), 4);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i64, clock.halfPeriod), 5);
          table = LLVM::InsertValueOp::create(
              initializer, location, table, entry,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return table;
      });
  return success();
}

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
  module.walk([&](sim::SimSpawnOp spawn) { ++spawnCounts[spawn.getCallee()]; });

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

    sim::SimFuncOp evalBody;
    if (auto evalBodyRef =
            target->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body")) {
      evalBody = design.lookupSymbol<sim::SimFuncOp>(evalBodyRef.getValue());
      if (!evalBody || evalBody.getBody().front().getNumArguments() !=
                           entry.getNumArguments()) {
        target.emitOpError("AOT eval body has an invalid capture signature");
        return WalkResult::interrupt();
      }
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

      auto specializeFunctionArgument = [&](sim::SimFuncOp function) {
        Block &functionEntry = function.getBody().front();
        SmallVector<OpOperand *> uses;
        for (OpOperand &use : functionEntry.getArgument(index).getUses())
          uses.push_back(&use);
        DenseMap<Block *, Value> specializedByBlock;
        for (OpOperand *use : uses) {
          Block *block = use->getOwner()->getBlock();
          auto [position, inserted] =
              specializedByBlock.try_emplace(block, Value{});
          if (inserted) {
            OpBuilder builder(function.getContext());
            builder.setInsertionPointToStart(block);
            IRMapping mapping;
            mapping.map(spawn.getOperand(0), functionEntry.getArgument(0));
            position->second = builder.clone(*producer, mapping)->getResult(0);
          }
          use->set(position->second);
        }
      };
      specializeFunctionArgument(target);
      if (evalBody)
        specializeFunctionArgument(evalBody);
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
    return std::tuple{lhs.static_state, lhs.low_bit,    lhs.bit_width,
                      lhs.edge,         lhs.actor_slot, lhs.continuation} <
           std::tuple{rhs.static_state, rhs.low_bit,    rhs.bit_width,
                      rhs.edge,         rhs.actor_slot, rhs.continuation};
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

FailureOr<NativeThreeTierPlan>
buildNativeThreeTierPlan(ModuleOp module,
                         const NativeStateLayout &stateLayout) {
  NativeThreeTierPlan result;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  if (!design)
    return result;
  auto schedule = design->getAttrOfType<sim::ThreeTierScheduleAttr>(
      sim::metadata::threeTierSchedule);
  if (!schedule)
    return result;
  if (schedule.getSourceGraph() != design.getComputeGraphAttr())
    return design.emitOpError("has stale three-tier schedule metadata"),
           failure();
  result.ownerCount = schedule.getOwnerCount();

  auto resolveRange = [&](sim::InductiveRootAttr root)
      -> FailureOr<std::optional<NativePromotionRange>> {
    const auto &handles =
        root.getResource() == sim::ComputeResourceKind::Storage
            ? stateLayout.storage
            : stateLayout.nets;
    auto handle = handles.find(root.getDescriptor());
    if (handle == handles.end())
      return design.emitOpError(
                 "promotion closure references an unknown native root"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return design.emitOpError(
                 "promotion closure has a non-static native root"),
             failure();
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.handleID == decoded.id;
    });
    if (bound == stateLayout.bounds.end())
      return design.emitOpError(
                 "promotion closure is absent from native state layout"),
             failure();
    // A physically two-state root has no mutable unknown bits and therefore
    // contributes nothing to the cold promotion scan.
    if (!bound->fourState)
      return std::optional<NativePromotionRange>{};
    return std::optional<NativePromotionRange>{
        NativePromotionRange{bound->offset, bound->width}};
  };

  for (Attribute attribute : schedule.getKernels()) {
    auto kernel = cast<sim::ScheduledKernelAttr>(attribute);
    NativeThreeTierKernelPlan planned;
    planned.id = kernel.getId();
    planned.owner = kernel.getOwner();
    planned.readyBit = kernel.getReadyBit();
    planned.tier = kernel.getTier();
    planned.schedule = kernel.getSchedule();
    planned.memberCount = static_cast<uint32_t>(kernel.getFragments().size());
    for (int64_t member : kernel.getFragments().asArrayRef()) {
      if (member < 0 || static_cast<uint64_t>(member) > UINT32_MAX)
        return design.emitOpError(
                   "three-tier kernel has an invalid fragment ID"),
               failure();
      planned.memberIDs.push_back(static_cast<uint32_t>(member));
    }
    planned.twoStateEligible = kernel.getTwoStateEligible();
    if (planned.twoStateEligible)
      for (Attribute rootAttribute : kernel.getPromotionRoots()) {
        FailureOr<std::optional<NativePromotionRange>> range =
            resolveRange(cast<sim::InductiveRootAttr>(rootAttribute));
        if (failed(range))
          return failure();
        if (*range)
          planned.promotionRanges.push_back(**range);
      }
    llvm::sort(planned.promotionRanges, [](const auto &lhs, const auto &rhs) {
      return std::tie(lhs.bitOffset, lhs.bitWidth) <
             std::tie(rhs.bitOffset, rhs.bitWidth);
    });
    planned.promotionRanges.erase(
        std::unique(planned.promotionRanges.begin(),
                    planned.promotionRanges.end(),
                    [](const auto &lhs, const auto &rhs) {
                      return lhs.bitOffset == rhs.bitOffset &&
                             lhs.bitWidth == rhs.bitWidth;
                    }),
        planned.promotionRanges.end());
    result.kernels.push_back(std::move(planned));
  }
  return result;
}

} // namespace obelisk::detail
