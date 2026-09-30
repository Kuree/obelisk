//===- SimulationAOTPlanning.cpp - Native AOT plan derivation -----------===//

#include "../SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

FailureOr<SmallVector<NativePeriodicClock>> buildNativePeriodicClockPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots) {
  SmallVector<NativePeriodicClock> clocks;
  // Dump operations may be in an untaken plusarg-controlled branch. Keep the
  // periodic plan: runtime preparation/checkpoints reject it when waveform
  // collection is actually active and needs once-per-slot publication.
  WalkResult result = module.walk([&](sim::SimFuncOp function) {
    auto actor = actorSlots.find(function.getOperation());
    if (actor == actorSlots.end() || function.isExternal() ||
        function.getBody().empty())
      return WalkResult::advance();

    SmallVector<sim::SimSuspendDelayOp> delays;
    SmallVector<sim::SimRefLoadOp> loads;
    SmallVector<sim::SimRefStoreOp> stores;
    SmallVector<sim::SimLogicUnaryOp> unaries;
    SmallVector<arith::XOrIOp> xors;
    function.walk([&](Operation *operation) {
      if (auto op = dyn_cast<sim::SimSuspendDelayOp>(operation))
        delays.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefLoadOp>(operation))
        loads.push_back(op);
      else if (auto op = dyn_cast<sim::SimRefStoreOp>(operation))
        stores.push_back(op);
      else if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation))
        unaries.push_back(op);
      else if (auto op = dyn_cast<arith::XOrIOp>(operation))
        xors.push_back(op);
    });
    if (delays.size() != 1 || loads.size() != 1 || stores.size() != 1 ||
        unaries.size() + xors.size() != 1 ||
        (!unaries.empty() &&
         unaries.front().getKind() != sim::UnaryKind::BitNot))
      return WalkResult::advance();

    sim::SimSuspendDelayOp delay = delays.front();
    auto period = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
    if (!period || period.getValue() == 0 || !delay.getTimingAttr() ||
        delay.getTimingAttr().getKind() !=
            schedule::ComputeTimingKind::Calendar ||
        delay.getContinuationOperands().size() != 0)
      return WalkResult::advance();
    sim::SimRefLoadOp load = loads.front();
    sim::SimRefStoreOp store = stores.front();
    Operation *toggleOperation = unaries.empty()
                                     ? xors.front().getOperation()
                                     : unaries.front().getOperation();
    if (store.getValue().getDefiningOp() != toggleOperation ||
        load.getReference() != store.getReference())
      return WalkResult::advance();
    if (!unaries.empty() && unaries.front().getInput() != load.getResult())
      return WalkResult::advance();
    if (!xors.empty()) {
      arith::XOrIOp xorOp = xors.front();
      Value other;
      if (xorOp.getLhs() == load.getResult())
        other = xorOp.getRhs();
      else if (xorOp.getRhs() == load.getResult())
        other = xorOp.getLhs();
      else
        return WalkResult::advance();
      auto one = other.getDefiningOp<arith::ConstantOp>();
      auto integer =
          one ? dyn_cast<IntegerAttr>(one.getValue()) : IntegerAttr{};
      if (!integer || integer.getValue().getBitWidth() != 1 ||
          !integer.getValue().isOne())
        return WalkResult::advance();
    }
    // Resolve a fixed packed subelement back to its physical root. Multiple
    // periodic generators may occupy distinct bits of the same descriptor;
    // the clock identity therefore includes this exact packed offset.
    Value physicalReference = load.getReference();
    uint64_t localBitOffset = 0;
    while (auto view =
               physicalReference.getDefiningOp<sim::SimRefSubelementOp>()) {
      Type type =
          cast<sim::RefType>(view.getInput().getType()).getElementType();
      for (int64_t index : view.getIndices()) {
        if (index < 0)
          return WalkResult::advance();
        auto child = sim::getAggregateProvenanceSubelement(
            type, static_cast<unsigned>(index));
        if (!child || child->first > UINT64_MAX - localBitOffset)
          return WalkResult::advance();
        localBitOffset += child->first;
        type = sim::getAggregateElementType(type, static_cast<unsigned>(index));
      }
      physicalReference = view.getInput();
    }
    auto reference = dyn_cast<BlockArgument>(physicalReference);
    auto storage = physicalReference.getDefiningOp<sim::SimContextStorageOp>();
    if ((!reference || reference.getOwner() != &function.getBody().front()) &&
        !storage)
      return WalkResult::advance();
    if (storage &&
        (function.getBody().front().getNumArguments() == 0 ||
         storage.getContext() != function.getBody().front().getArgument(0)))
      return WalkResult::advance();
    auto refType = dyn_cast<sim::RefType>(load.getReference().getType());
    Type elementType = refType ? refType.getElementType() : Type{};
    auto logicType = dyn_cast_if_present<sim::LogicType>(elementType);
    auto integerType = dyn_cast_if_present<IntegerType>(elementType);
    if ((!logicType || logicType.getWidth() != 1) &&
        (!integerType || integerType.getWidth() != 1))
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
    SmallVector<Operation *> toggleOperations;
    for (Operation &operation : toggle->without_terminator())
      // Fixed-capture specialization projects the physical clock reference
      // from the context in this block. It is addressing metadata, not an
      // extra clock action; the executable sequence must still be exactly
      // load -> not -> store.
      if (!isa<sim::SimContextStorageOp, sim::SimRefSubelementOp,
               arith::ConstantOp, sim::SimCoveragePointHitOp>(operation))
        toggleOperations.push_back(&operation);
    if (toggleOperations.size() != 3 || toggleOperations[0] != load ||
        toggleOperations[1] != toggleOperation || toggleOperations[2] != store)
      return WalkResult::advance();
    bool unsupported = false;
    SmallVector<uint64_t> coveragePoints;
    function.walk([&](Operation *operation) {
      if (operation == function.getOperation())
        return;
      if (auto hit = dyn_cast<sim::SimCoveragePointHitOp>(operation)) {
        if (hit->getBlock() != wait && hit->getBlock() != toggle) {
          // Entry instrumentation runs once in the bootstrap coroutine.
          unsupported |= hit->getBlock() != &function.getBody().front();
          return;
        }
        auto enabled = hit.getEnabled().getDefiningOp<arith::ConstantOp>();
        auto value =
            enabled ? dyn_cast<IntegerAttr>(enabled.getValue()) : IntegerAttr{};
        if (!value) {
          unsupported = true;
        } else if (!value.getValue().isZero()) {
          coveragePoints.push_back(hit.getPoint());
        }
        return;
      }
      if (isa<sim::SimSuspendDelayOp, sim::SimRefLoadOp, sim::SimRefStoreOp,
              sim::SimLogicUnaryOp, sim::SimTimeConstantOp,
              sim::SimContextStorageOp, arith::XOrIOp, arith::ConstantOp,
              cf::BranchOp>(operation))
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
      if (auto low = function.getArgAttrOfType<IntegerAttr>(
              reference.getArgNumber(), sim::metadata::descriptorLow)) {
        if (low.getValue().isNegative() ||
            low.getValue().getActiveBits() > 64 ||
            low.getUInt() > UINT64_MAX - localBitOffset)
          return WalkResult::advance();
        localBitOffset += low.getUInt();
      }
    } else {
      descriptor = storage.getId();
    }
    if (!descriptor)
      return WalkResult::advance();
    auto rootOffset = stateLayout.storageOffsets.find(*descriptor);
    if (rootOffset == stateLayout.storageOffsets.end() ||
        localBitOffset > UINT64_MAX - rootOffset->second)
      return WalkResult::advance();
    uint64_t bitOffset = rootOffset->second + localBitOffset;
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.offset <= bitOffset &&
             bitOffset - candidate.offset < candidate.width;
    });
    if (bound == stateLayout.bounds.end())
      return WalkResult::advance();
    auto site = delay.getSiteAttr();
    if (!site || site.getId() == 0)
      return delay.emitOpError("periodic clock wait has no continuation ID"),
             WalkResult::interrupt();
    clocks.push_back(NativePeriodicClock::get(
        module.getContext(), actor->second, site.getId(), bound->handleID,
        bitOffset, period.getValue(), coveragePoints));
    return WalkResult::advance();
  });
  if (result.wasInterrupted())
    return failure();
  llvm::sort(clocks, [](const NativePeriodicClock &lhs,
                        const NativePeriodicClock &rhs) {
    return std::tuple{lhs.getHalfPeriod(), lhs.getStaticState(),
                      lhs.getBitOffset(), lhs.getActorSlot()} <
           std::tuple{rhs.getHalfPeriod(), rhs.getStaticState(),
                      rhs.getBitOffset(), rhs.getActorSlot()};
  });
  DenseSet<std::pair<uint32_t, uint64_t>> physicalBits;
  for (const NativePeriodicClock &clock : clocks)
    if (!physicalBits.insert({clock.getStaticState(), clock.getBitOffset()})
             .second)
      return module.emitError(
                 "multiple periodic generators drive the same physical bit"),
             failure();
  return clocks;
}

llvm::DenseMap<analysis::ClockBit, analysis::ClockFact>
buildNativeClockInferencePlan(
    ModuleOp module, const NativeStateLayout &layout,
    const DenseMap<Operation *, uint32_t> &actorSlots,
    ArrayRef<NativePeriodicClock> clocks) {
  SmallVector<analysis::PeriodicClockSeed> seeds;
  for (const auto &clock : clocks) {
    auto actor = llvm::find_if(actorSlots, [&](const auto &entry) {
      return entry.second == clock.getActorSlot();
    });
    if (actor != actorSlots.end())
      seeds.push_back({{clock.getStaticState(), clock.getBitOffset()},
                       actor->first, clock.getHalfPeriod()});
  }
  analysis::ClockInferenceAnalysis inference(module, layout, seeds);
  llvm::DenseSet<analysis::ClockBit> periodic;
  for (const auto &[bit, fact] : inference.getFacts())
    if (fact.hasTickBound())
      periodic.insert(bit);
  if (module->hasAttr("obelisk.debug.native_timing"))
    llvm::errs() << "obelisk periodic signal proof: clocks=" << clocks.size()
                 << " copies=" << inference.getCopyCount()
                 << " cadence-outputs=" << inference.getCadenceCount()
                 << " periodic-bits=" << periodic.size() << '\n';
  return inference.getFacts();
}

FailureOr<SmallVector<NativePeriodicAlias>>
buildNativePeriodicAliasPlan(ModuleOp module,
                             const NativeStateLayout &stateLayout,
                             const DenseMap<Operation *, uint32_t> &actorSlots,
                             ArrayRef<NativePeriodicClock> periodicClocks) {
  SmallVector<NativePeriodicAlias> aliases;
  if (periodicClocks.empty())
    return aliases;

  auto descriptorFor = [](sim::SimFuncOp function, Value value,
                          bool storage) -> std::optional<uint64_t> {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      if (argument.getOwner() != &function.getBody().front())
        return std::nullopt;
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::descriptorId);
      if (descriptor)
        return descriptor.getValue().getZExtValue();
      return std::nullopt;
    }
    if (storage)
      if (auto lookup = value.getDefiningOp<sim::SimContextStorageOp>())
        return lookup.getId();
    if (!storage)
      if (auto lookup = value.getDefiningOp<sim::SimContextDriverOp>())
        return lookup.getId();
    return std::nullopt;
  };
  auto decodeStaticRoot = [&](uint64_t handle) -> std::optional<uint32_t> {
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return std::nullopt;
    return decoded.id;
  };

  WalkResult walked = module.walk([&](sim::SimFuncOp function) {
    auto actor = actorSlots.find(function.getOperation());
    if (actor == actorSlots.end() || function.isExternal() ||
        function.getBody().empty())
      return WalkResult::advance();

    SmallVector<sim::SimRefLoadOp> loads;
    SmallVector<sim::SimDriverDriveOp> drives;
    SmallVector<sim::SimSuspendChangeOp> waits;
    bool unsupported = false;
    function.walk([&](Operation *operation) {
      if (operation == function.getOperation())
        return;
      if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
        loads.push_back(load);
      else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation))
        drives.push_back(drive);
      else if (auto wait = dyn_cast<sim::SimSuspendChangeOp>(operation))
        waits.push_back(wait);
      else if (!isa<sim::SimContextStorageOp, sim::SimContextDriverOp,
                    sim::SimRefSubelementOp, cf::BranchOp>(operation))
        unsupported = true;
    });
    if (unsupported || loads.size() != 1 || drives.size() != 1 ||
        waits.size() != 1)
      return WalkResult::advance();

    sim::SimRefLoadOp load = loads.front();
    sim::SimDriverDriveOp drive = drives.front();
    sim::SimSuspendChangeOp wait = waits.front();
    Value sourceReference = load.getReference();
    uint64_t sourceLocalBitOffset = 0;
    while (auto view =
               sourceReference.getDefiningOp<sim::SimRefSubelementOp>()) {
      Type type =
          cast<sim::RefType>(view.getInput().getType()).getElementType();
      for (int64_t index : view.getIndices()) {
        if (index < 0) {
          unsupported = true;
          break;
        }
        auto child = sim::getAggregateProvenanceSubelement(
            type, static_cast<unsigned>(index));
        if (!child || child->first > UINT64_MAX - sourceLocalBitOffset) {
          unsupported = true;
          break;
        }
        sourceLocalBitOffset += child->first;
        type = sim::getAggregateElementType(type, static_cast<unsigned>(index));
      }
      if (unsupported)
        break;
      sourceReference = view.getInput();
    }
    if (auto argument = dyn_cast<BlockArgument>(sourceReference)) {
      if (argument.getOwner() != &function.getBody().front())
        return WalkResult::advance();
      if (auto low = function.getArgAttrOfType<IntegerAttr>(
              argument.getArgNumber(), sim::metadata::descriptorLow)) {
        if (low.getValue().isNegative() ||
            low.getValue().getActiveBits() > 64 ||
            low.getUInt() > UINT64_MAX - sourceLocalBitOffset)
          return WalkResult::advance();
        sourceLocalBitOffset += low.getUInt();
      }
    }
    if (unsupported || load->getBlock() != drive->getBlock() ||
        load->getBlock() != wait->getBlock() ||
        drive.getValue() != load.getResult() ||
        (wait.getWatched() != load.getReference() &&
         wait.getWatched() != sourceReference) ||
        wait.getContinuation() != wait->getBlock() ||
        !wait.getContinuationOperands().empty())
      return WalkResult::advance();

    std::optional<uint64_t> sourceDescriptor =
        descriptorFor(function, sourceReference, true);
    std::optional<uint64_t> driverDescriptor =
        descriptorFor(function, drive.getDriver(), false);
    if (!sourceDescriptor || !driverDescriptor)
      return WalkResult::advance();
    auto sourceHandle = stateLayout.storage.find(*sourceDescriptor);
    auto sourceRootOffset = stateLayout.storageOffsets.find(*sourceDescriptor);
    auto driverOffset = stateLayout.driverOffsets.find(*driverDescriptor);
    auto driver =
        llvm::find_if(stateLayout.driverLayouts, [&](const auto &candidate) {
          return candidate.id == *driverDescriptor;
        });
    if (sourceHandle == stateLayout.storage.end() ||
        sourceRootOffset == stateLayout.storageOffsets.end() ||
        sourceLocalBitOffset > UINT64_MAX - sourceRootOffset->second ||
        driverOffset == stateLayout.driverOffsets.end() ||
        driver == stateLayout.driverLayouts.end() || driver->width != 1 ||
        driver->drivenLow != 0 || driver->drivenWidth != 1)
      return WalkResult::advance();
    // The alias copies four-state planes without resolution. That is only an
    // identity when neither known polarity is replaced by high impedance.
    if (driver->strength0 == sim::Strength::HighZ ||
        driver->strength1 == sim::Strength::HighZ)
      return WalkResult::advance();
    std::optional<uint32_t> sourceStatic =
        decodeStaticRoot(sourceHandle->second);
    if (!sourceStatic)
      return WalkResult::advance();
    auto clock = llvm::find_if(periodicClocks, [&](const auto &candidate) {
      uint64_t sourceBitOffset =
          sourceRootOffset->second + sourceLocalBitOffset;
      return candidate.getStaticState() == *sourceStatic &&
             candidate.getBitOffset() == sourceBitOffset;
    });
    if (clock == periodicClocks.end())
      return WalkResult::advance();

    // Directly updating a resolved net is equivalent to the drive only for a
    // one-bit net with this exact single driver.  This is the semantic fact an
    // effect summary cannot establish: the SSA body above additionally proves
    // that both value and unknown planes are copied unchanged.
    if (llvm::count_if(stateLayout.driverLayouts, [&](const auto &candidate) {
          return candidate.netId == driver->netId;
        }) != 1)
      return WalkResult::advance();
    auto net =
        llvm::find_if(stateLayout.netLayouts, [&](const auto &candidate) {
          return candidate.id == driver->netId;
        });
    auto netHandle = stateLayout.nets.find(driver->netId);
    auto netOffset = stateLayout.netOffsets.find(driver->netId);
    if (net == stateLayout.netLayouts.end() || net->width != 1 ||
        netHandle == stateLayout.nets.end() ||
        netOffset == stateLayout.netOffsets.end())
      return WalkResult::advance();
    std::optional<uint32_t> targetStatic = decodeStaticRoot(netHandle->second);
    auto site = wait.getSiteAttr();
    if (!targetStatic || !site || site.getId() == 0)
      return WalkResult::advance();

    aliases.push_back(NativePeriodicAlias::get(
        module.getContext(), *sourceStatic, actor->second, site.getId(),
        *targetStatic, sourceRootOffset->second + sourceLocalBitOffset,
        netOffset->second, driverOffset->second));
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  llvm::sort(aliases, [](const NativePeriodicAlias &lhs,
                         const NativePeriodicAlias &rhs) {
    return std::tuple(lhs.getSourceStaticState(), lhs.getSourceBitOffset(),
                      lhs.getForwardingActorSlot(),
                      lhs.getForwardingContinuation(),
                      lhs.getTargetStaticState()) <
           std::tuple(rhs.getSourceStaticState(), rhs.getSourceBitOffset(),
                      rhs.getForwardingActorSlot(),
                      rhs.getForwardingContinuation(),
                      rhs.getTargetStaticState());
  });
  aliases.erase(std::unique(aliases.begin(), aliases.end(),
                            [](const auto &lhs, const auto &rhs) {
                              return lhs.getSourceStaticState() ==
                                         rhs.getSourceStaticState() &&
                                     lhs.getSourceBitOffset() ==
                                         rhs.getSourceBitOffset() &&
                                     lhs.getForwardingActorSlot() ==
                                         rhs.getForwardingActorSlot() &&
                                     lhs.getForwardingContinuation() ==
                                         rhs.getForwardingContinuation() &&
                                     lhs.getTargetStaticState() ==
                                         rhs.getTargetStaticState();
                            }),
                aliases.end());
  return aliases;
}

FailureOr<SmallVector<obelisk_rt_static_actor_root>>
buildNativeStaticActorRootPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    const DenseMap<Operation *, uint32_t> &actorSlots,
    const DenseSet<uint64_t> &checkpointOnlyActors) {
  SmallVector<obelisk_rt_static_actor_root> plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  auto specialization =
      design
          ? ::obelisk::schedule::get<schedule::metadata::staticSpecialization>(
                design)
          : schedule::StaticSpecializationAttr{};
  if (!specialization)
    return plan;
  SymbolTable symbols(design);
  for (Attribute attribute : specialization.getActorRoots()) {
    auto dependency = dyn_cast<schedule::StaticActorRootAttr>(attribute);
    if (!dependency)
      return module.emitError("invalid static actor/root dependency"),
             failure();
    sim::SimFuncOp function =
        symbols.lookup<sim::SimFuncOp>(dependency.getFunction().getValue());
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || actor == actorSlots.end())
      continue;
    IntegerAttr codeUnit = function.getCodeUnitIdAttr();
    // IEEE 1800-2017 Clauses 31.7 and 31.9.1 require these exact source
    // publications to pass through a runtime checkpoint. Do not attribute a
    // checkpoint-owned write to the runtime-free direct actor when certifying
    // generated primary reads.
    if (codeUnit && checkpointOnlyActors.contains(codeUnit.getUInt()))
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
    const DenseMap<Operation *, uint32_t> &actorSlots,
    const DenseMap<Operation *, SmallVector<Block *>> &bytecodeFragments,
    const DenseSet<Operation *> &runtimeOwnedFanoutActors, bool enabled,
    bool certifiedStaticIsland) {
  NativeStaticFanoutPlan plan;
  plan.exact = enabled;
  if (!enabled)
    return plan;
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
  schedule::ComputeGraphAttr graph =
      design ? design.getComputeGraphAttr() : nullptr;
  if (!graph)
    return module.emitError("static fanout plan requires a compute graph"),
           failure();
  SymbolTable symbols(design);
  DenseMap<uint32_t, SmallVector<uint32_t>> resumeTargets;
  DenseMap<uint32_t, SmallVector<uint32_t>> processSuccessors;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == schedule::ComputeEdgeKind::Resume)
      resumeTargets[edge.getSource()].push_back(edge.getTarget());
    else if (edge.getKind() == schedule::ComputeEdgeKind::ProcessOrder)
      processSuccessors[edge.getSource()].push_back(edge.getTarget());
  }
  auto disableExactFanout = [&](StringRef reason,
                                sim::SimFuncOp function = {}) {
    if (module->hasAttr("obelisk.debug.native_timing")) {
      llvm::errs() << "static fanout disabled: " << reason;
      if (function)
        llvm::errs() << " in " << function.getSymName();
      llvm::errs() << '\n';
    }
    plan.entries.clear();
    plan.fragments.clear();
    plan.runtimeTransitionStates.clear();
    plan.exact = false;
  };
  auto resumeClosure = [&](uint32_t suspension) {
    SmallVector<uint32_t> result;
    SmallVector<uint32_t> pending;
    if (auto found = resumeTargets.find(suspension);
        found != resumeTargets.end())
      llvm::append_range(pending, found->second);
    llvm::SmallDenseSet<uint32_t, 16> seen;
    // The resume closure describes one activation body. Do not follow the
    // process-order backedge into the suspension that owns the next event.
    seen.insert(suspension);
    while (!pending.empty()) {
      uint32_t fragment = pending.pop_back_val();
      if (!seen.insert(fragment).second)
        continue;
      result.push_back(fragment);
      if (auto found = processSuccessors.find(fragment);
          found != processSuccessors.end())
        llvm::append_range(pending, found->second);
    }
    llvm::sort(result);
    return result;
  };
  for (auto [fragmentIndex, node] : llvm::enumerate(graph.getNodes())) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    SmallVector<schedule::ComputeEffectAttr> watches;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<schedule::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() == schedule::ComputeEffectKind::Watch)
        watches.push_back(effect);
    }
    if (watches.empty())
      continue;
    sim::SimFuncOp function =
        symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
    Block *block =
        function
            ? analysis::lookupComputeGraphBlock(function, fragment.getBlock())
            : nullptr;
    auto actor =
        function ? actorSlots.find(function.getOperation()) : actorSlots.end();
    if (!function || !block)
      return module.emitError(
                 "static fanout references a stale compute fragment"),
             failure();
    auto bytecode = bytecodeFragments.find(function.getOperation());
    bool bytecodeBlock = bytecode != bytecodeFragments.end() &&
                         llvm::is_contained(bytecode->second, block);
    // Every actor outside the admitted island retains its runtime watch.
    // Exact descriptor roots enter the transition bridge so direct stores
    // still wake those subscriptions in the same time slot. The generated
    // covergroup registration has abstract graph watches and needs its
    // separate operand-based root reconstruction below.
    if ((actor == actorSlots.end() || bytecodeBlock) && certifiedStaticIsland) {
      bool hasConditionalWait = false;
      if (!runtimeOwnedFanoutActors.contains(function.getOperation()))
        function.walk([&](Operation *operation) {
          if (isa<sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp,
                  sim::SimSuspendClockSetOp, sim::SimSuspendObserveOp,
                  sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
                  sim::SimCovergroupClockEventRegisterOp>(operation))
            hasConditionalWait = true;
        });
      if (hasConditionalWait) {
        disableExactFanout("runtime watch needs conditional scheduling",
                           function);
        continue;
      }
      if (runtimeOwnedFanoutActors.contains(function.getOperation()) &&
          ::obelisk::schedule::has<
              ::obelisk::schedule::Field::CovergroupClockingSampler>(
              function)) {
        // Computed covergroup primaries expose an abstract graph Watch: the
        // runtime indexes their exact static dependencies from the serialized
        // observer plan. Reconstruct those same roots from the bound observer
        // operands so direct AOT publications retain the transition bridge
        // without pretending that the cold actor owns a generated slot.
        analysis::DescriptorProvenanceMap provenance =
            analysis::deriveDescriptorProvenance(function);
        auto registrations =
            function.getOps<sim::SimCovergroupClockEventRegisterOp>();
        auto registration =
            llvm::find_if(registrations, [](auto) { return true; });
        if (registration == registrations.end()) {
          disableExactFanout("covergroup clock actor has no registration",
                             function);
          continue;
        }
        sim::SimCovergroupClockEventRegisterOp registrationOp = *registration;
        // Only primary expressions subscribe to publications. An `iff`
        // expression is evaluated when one of those primaries fires and reads
        // its other inputs from canonical state; it must not make those inputs
        // independent transition roots.
        SmallVector<Value> observers(registrationOp.getPrimaries());
        bool valid = true;
        for (Value observer : observers) {
          auto binding = observer.getDefiningOp<sim::SimObserverBindOp>();
          if (!binding) {
            valid = false;
            break;
          }
          for (Value dependency : binding.getDependencies()) {
            auto found = provenance.find(dependency);
            if (found == provenance.end() || !found->second.descriptor ||
                found->second.dynamic ||
                (found->second.resource !=
                     schedule::ComputeResourceKind::Storage &&
                 found->second.resource !=
                     schedule::ComputeResourceKind::Net)) {
              valid = false;
              break;
            }
            uint64_t width = found->second.width != 0 ? found->second.width
                                                      : found->second.rootWidth;
            if (width == 0 || found->second.low > found->second.rootWidth ||
                width > found->second.rootWidth - found->second.low) {
              valid = false;
              break;
            }
            const auto &handles =
                found->second.resource == schedule::ComputeResourceKind::Storage
                    ? stateLayout.storage
                    : stateLayout.nets;
            auto handle = handles.find(*found->second.descriptor);
            obelisk_rt_stable_handle_v1 decoded{};
            if (handle == handles.end() ||
                !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
                decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
                decoded.offset != 0) {
              valid = false;
              break;
            }
            auto bound =
                llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
                  return candidate.handleID == decoded.id;
                });
            if (bound == stateLayout.bounds.end() ||
                found->second.low > bound->width ||
                width > bound->width - found->second.low) {
              valid = false;
              break;
            }
            plan.runtimeTransitionStates.insert(decoded.id);
          }
          if (!valid)
            break;
        }
        if (!valid)
          disableExactFanout(
              "covergroup clock observer dependency is not statically bound",
              function);
        continue;
      }
      // Runtime waits on excluded actors have no generated fanout entry or
      // actor slot, but their watched roots must still publish transitions.
      for (schedule::ComputeEffectAttr effect : watches) {
        if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
            effect.getDynamic() || effect.getDeferred() ||
            (effect.getResource() != schedule::ComputeResourceKind::Storage &&
             effect.getResource() != schedule::ComputeResourceKind::Net) ||
            effect.getWidth() == 0) {
          disableExactFanout("runtime watch is not statically bound", function);
          continue;
        }
        const auto &handles =
            effect.getResource() == schedule::ComputeResourceKind::Storage
                ? stateLayout.storage
                : stateLayout.nets;
        auto handle = handles.find(effect.getDescriptor());
        if (handle == handles.end())
          return block->getTerminator()->emitError(
                     "runtime fanout references unknown state"),
                 failure();
        obelisk_rt_stable_handle_v1 decoded{};
        if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
            decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
            decoded.offset != 0)
          return block->getTerminator()->emitError(
                     "runtime fanout has an invalid native root"),
                 failure();
        auto bound =
            llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
              return candidate.handleID == decoded.id;
            });
        if (bound == stateLayout.bounds.end() ||
            effect.getLow() > bound->width ||
            effect.getWidth() > bound->width - effect.getLow())
          return block->getTerminator()->emitError(
                     "runtime fanout range is out of bounds"),
                 failure();
        // A runtime-owned observer has no generated fanout entry or actor
        // slot. Keep the watched root on the v1 transition bridge so direct
        // static and periodic publications still reach that observer before
        // the generated scheduler advances beyond this time slot.
        plan.runtimeTransitionStates.insert(decoded.id);
      }
      continue;
    }
    if (actor == actorSlots.end()) {
      disableExactFanout("watched actor has no static slot", function);
      continue;
    }
    Operation *terminator = block->getTerminator();
    schedule::ContinuationSiteAttr site;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(terminator))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendClockSetOp>(terminator)) {
      // IEEE 1800-2017 31.7 samples `&&&` only after a primary timing event.
      // The compute graph therefore exposes only primary Watch effects here;
      // a compiled condition read must not become a fanout dependency.
      site = suspend.getSiteAttr();
    } else {
      disableExactFanout("watch is not owned by a fixed suspension", function);
      continue;
    }
    if (!site || site.getId() == 0)
      return terminator->emitError(
                 "static fanout suspension has no continuation metadata"),
             failure();
    auto &fragments = plan.fragments[{actor->second, site.getId()}];
    uint32_t graphIndex = static_cast<uint32_t>(fragmentIndex);
    // Keep the suspension itself as the stable physical ownership anchor.
    // Fusion may consume the Resume edge while retaining the source fragment
    // in the direct body's FragmentABI, in which case the post-resume closure
    // is empty but ownership is still exact and unambiguous.
    fragments.push_back(graphIndex);
    SmallVector<uint32_t> activation = resumeClosure(graphIndex);
    llvm::append_range(fragments, activation);
    llvm::sort(fragments);
    fragments.erase(std::unique(fragments.begin(), fragments.end()),
                    fragments.end());
    for (schedule::ComputeEffectAttr effect : watches) {
      if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
          effect.getDynamic() || effect.getDeferred() ||
          (effect.getResource() != schedule::ComputeResourceKind::Storage &&
           effect.getResource() != schedule::ComputeResourceKind::Net)) {
        disableExactFanout("watch has dynamic descriptor provenance", function);
        continue;
      }
      const auto &handles =
          effect.getResource() == schedule::ComputeResourceKind::Storage
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
      // IEEE 1800-2023 4.5, 9.4.2: preserve the static actor/continuation
      // entry, but publish through the runtime for Reactive consumers.
      // Their owner is outside the generated Active closure, so a generated
      // ready bit alone cannot deliver the event in its proper region.
      if (function.getHomeRegion() == sim::EventRegion::Reactive)
        plan.runtimeTransitionStates.insert(decoded.id);
      uint32_t edge;
      switch (effect.getTrigger()) {
      case schedule::ComputeTriggerKind::Change:
        edge = OBELISK_RT_WAIT_EDGE_CHANGE;
        break;
      case schedule::ComputeTriggerKind::Posedge:
        edge = OBELISK_RT_WAIT_EDGE_POSEDGE;
        break;
      case schedule::ComputeTriggerKind::Negedge:
        edge = OBELISK_RT_WAIT_EDGE_NEGEDGE;
        break;
      case schedule::ComputeTriggerKind::Both:
        edge = OBELISK_RT_WAIT_EDGE_BOTH;
        break;
      default:
        disableExactFanout("watch has unsupported trigger", function);
        continue;
      }
      bool suppressActiveSelf =
          (::obelisk::schedule::has<schedule::metadata::proceduralEventWait>(
               terminator) ||
           ::obelisk::schedule::has<schedule::metadata::topLevelWildcardWait>(
               terminator)) &&
          !::obelisk::schedule::has<schedule::metadata::repeatingAlwaysWait>(
              terminator);
      uint32_t flags =
          suppressActiveSelf ? OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF : 0;
      // IEEE 1800-2017 9.4.2 defines a vector edge event in terms of the
      // expression's least significant bit. Keep the full watched range for
      // ordinary change sensitivity, but encode edge-sensitive fanout as its
      // single LSB so every static publication path shares that semantic.
      uint64_t fanoutWidth =
          edge == OBELISK_RT_WAIT_EDGE_CHANGE ? effect.getWidth() : 1;
      plan.entries.push_back({decoded.id, actor->second, site.getId(), edge,
                              UINT32_MAX, flags, effect.getLow(), fanoutWidth});
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
  auto schedule =
      ::obelisk::schedule::get<schedule::metadata::threeTierSchedule>(design);
  if (!schedule)
    return result;
  if (schedule.getSourceGraph() != design.getComputeGraphAttr())
    return design.emitOpError("has stale three-tier schedule metadata"),
           failure();
  result.ownerCount = schedule.getOwnerCount();
  result.sourceGraph = schedule.getSourceGraph();

  auto resolveRange = [&](schedule::InductiveRootAttr root)
      -> FailureOr<std::optional<NativePromotionRange>> {
    const auto &handles =
        root.getResource() == schedule::ComputeResourceKind::Storage
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
    auto kernel = cast<schedule::ScheduledKernelAttr>(attribute);
    NativeThreeTierKernelPlan planned;
    planned.id = kernel.getId();
    planned.owner = kernel.getOwner();
    planned.readyBit = kernel.getReadyBit();
    planned.tier = kernel.getTier();
    planned.schedule = kernel.getSchedule();
    planned.loweringReady = kernel.getLoweringReady();
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
            resolveRange(cast<schedule::InductiveRootAttr>(rootAttribute));
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
  for (Attribute attribute : schedule.getIngress()) {
    auto ingress = cast<schedule::SchedulerIngressAttr>(attribute);
    result.ingress.push_back(
        {ingress.getFragment(), ingress.getOwner(), ingress.getReadyBit()});
  }
  return result;
}

FailureOr<NativeEvalOwnershipPlan>
buildNativeEvalOwnershipPlan(ModuleOp module,
                             const NativeStateLayout &stateLayout,
                             const NativeStaticFanoutPlan &fanoutPlan,
                             ArrayRef<NativeDirectFragment> directFragments,
                             ArrayRef<NativePeriodicAlias> periodicAliases) {
  NativeEvalOwnershipPlan result;
  result.fanoutOwners.reserve(fanoutPlan.entries.size());
  result.periodicFanoutOwners.reserve(fanoutPlan.entries.size());

  auto isPeriodicAlias = [&](const obelisk_rt_static_fanout_entry &entry) {
    return llvm::any_of(periodicAliases, [&](const NativePeriodicAlias &alias) {
      if (alias.getSourceStaticState() != entry.static_state ||
          alias.getForwardingActorSlot() != entry.actor_slot ||
          alias.getForwardingContinuation() != entry.continuation)
        return false;
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == entry.static_state;
          });
      if (bound == stateLayout.bounds.end() ||
          alias.getSourceBitOffset() < bound->offset)
        return false;
      uint64_t localBit = alias.getSourceBitOffset() - bound->offset;
      return entry.low_bit <= localBit &&
             localBit - entry.low_bit < entry.bit_width;
    });
  };

  auto fragmentsFor = [&](const obelisk_rt_static_fanout_entry &entry) {
    auto fragments =
        fanoutPlan.fragments.find({entry.actor_slot, entry.continuation});
    return fragments == fanoutPlan.fragments.end()
               ? ArrayRef<uint32_t>{}
               : ArrayRef<uint32_t>(fragments->second);
  };
  // A fused instance coordinator replaces each physical source activation in
  // its certified fragment set. Other direct bodies from those source actors
  // can remain as callable helpers, but must not independently own fanout.
  // Index coordinator coverage once so the common ownership path is linear.
  DenseMap<uint32_t, unsigned> coordinatorOwners;
  for (auto [index, candidate] : llvm::enumerate(directFragments)) {
    if (!candidate.instanceCoordinator)
      continue;
    for (uint32_t fragment : candidate.fragmentIDs) {
      auto [entry, inserted] =
          coordinatorOwners.try_emplace(fragment, static_cast<unsigned>(index));
      if (!inserted && entry->second != index)
        return module.emitError(
                   "eval fragment belongs to multiple instance coordinators"),
               failure();
    }
  }

  // Index the remaining graph certificates by fragment. Each physical owner
  // is resolved once below by counting only candidates mentioned by its
  // fragments; this avoids the previous fanout x body x fragment scan.
  DenseMap<uint32_t, SmallVector<unsigned>> fragmentCandidates;
  for (auto [index, candidate] : llvm::enumerate(directFragments))
    for (uint32_t fragment : candidate.fragmentIDs)
      fragmentCandidates[fragment].push_back(index);
  DenseMap<std::pair<uint32_t, uint32_t>, NativeEvalFanoutOwner> fallbackOwners;

  // Preserve physical actor/site identity when fusion did not absorb the
  // activation. This avoids comparing every ordinary fanout entry with every
  // generated body.
  DenseMap<std::pair<uint32_t, uint32_t>, unsigned> exactOwners;
  for (auto [index, candidate] : llvm::enumerate(directFragments)) {
    auto key = std::pair{candidate.actorSlot, candidate.continuation};
    auto [entry, inserted] =
        exactOwners.try_emplace(key, static_cast<unsigned>(index));
    if (!inserted && entry->second != index)
      return module.emitError(
                 "physical scheduler owner maps to multiple generated bodies"),
             failure();
  }

  for (const obelisk_rt_static_fanout_entry &entry : fanoutPlan.entries) {
    if (isPeriodicAlias(entry)) {
      NativeEvalFanoutOwner owner{NativeEvalFanoutOwnerKind::PeriodicAlias,
                                  UINT32_MAX};
      result.fanoutOwners.push_back(owner);
      result.periodicFanoutOwners.push_back(owner);
      continue;
    }

    ArrayRef<uint32_t> plannedFragments = fragmentsFor(entry);
    if (plannedFragments.empty()) {
      result.fanoutOwners.emplace_back();
      result.periodicFanoutOwners.emplace_back();
      continue;
    }
    std::optional<unsigned> coordinator;
    bool coordinatorCovers = true;
    for (uint32_t fragment : plannedFragments) {
      auto owner = coordinatorOwners.find(fragment);
      if (owner == coordinatorOwners.end()) {
        coordinatorCovers = false;
        break;
      }
      if (coordinator && *coordinator != owner->second)
        return module.emitError(
                   "typed scheduler owner crosses instance coordinators"),
               failure();
      coordinator = owner->second;
    }
    std::optional<unsigned> exact;
    if (auto foundExact =
            exactOwners.find({entry.actor_slot, entry.continuation});
        foundExact != exactOwners.end())
      exact = foundExact->second;
    auto physicalOwner = std::pair{entry.actor_slot, entry.continuation};
    NativeEvalFanoutOwner genericOwner;
    if (exact) {
      genericOwner = {NativeEvalFanoutOwnerKind::Direct,
                      static_cast<uint32_t>(*exact)};
    } else if (coordinatorCovers && coordinator) {
      genericOwner = {NativeEvalFanoutOwnerKind::Direct,
                      static_cast<uint32_t>(*coordinator)};
    } else if (auto cached = fallbackOwners.find(physicalOwner);
               cached != fallbackOwners.end()) {
      genericOwner = cached->second;
    } else {
      DenseMap<unsigned, unsigned> coverage;
      for (uint32_t fragment : plannedFragments)
        for (unsigned candidate : fragmentCandidates.lookup(fragment))
          ++coverage[candidate];
      std::optional<unsigned> direct;
      for (auto [candidate, count] : coverage) {
        if (count != plannedFragments.size())
          continue;
        if (direct && *direct != candidate)
          return module.emitError(
                     "typed scheduler owner maps to multiple generated bodies"),
                 failure();
        direct = candidate;
      }
      // Complete coverage in the current graph generation is the only
      // fallback after exact physical identity and a certified coordinator.
      genericOwner =
          direct ? NativeEvalFanoutOwner{NativeEvalFanoutOwnerKind::Direct,
                                         static_cast<uint32_t>(*direct)}
                 : NativeEvalFanoutOwner{};
      fallbackOwners.try_emplace(physicalOwner, genericOwner);
    }
    result.fanoutOwners.push_back(genericOwner);
    result.periodicFanoutOwners.push_back(
        coordinatorCovers && coordinator
            ? NativeEvalFanoutOwner{NativeEvalFanoutOwnerKind::Direct,
                                    static_cast<uint32_t>(*coordinator)}
            : genericOwner);
  }
  return result;
}

} // namespace obelisk::detail
