//===- SimulationAttributes.cpp - Attribute verifiers ---------===//
//
// Verifiers for the simulation dialect attributes that describe compute
// effects, fragments, kernels, and the three-tier schedule.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::sim {

LogicalResult RandomVariableReferenceAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ArrayRef<FlatSymbolRefAttr> path, FlatSymbolRefAttr target) {
  if (!target)
    return emitError() << "random-variable reference requires a target field";
  if (llvm::any_of(path, [](FlatSymbolRefAttr field) { return !field; }))
    return emitError()
           << "random-variable reference path contains a null field";
  return success();
}

static LogicalResult
verifyEffectArray(llvm::function_ref<InFlightDiagnostic()> emitError,
                  ArrayAttr effects, StringRef owner) {
  if (!effects)
    return emitError() << owner << " requires an effect array";
  if (llvm::any_of(effects, [](Attribute attr) {
        return !isa<ComputeEffectAttr>(attr);
      }))
    return emitError() << owner << " contains a non-effect attribute";
  return success();
}

LogicalResult ComputeEffectAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ComputeEffectKind effect, ComputeResourceKind resource,
    ComputeTargetKind target, uint64_t descriptor, uint32_t formal,
    uint64_t low, uint64_t width, bool dynamic, bool deferred,
    ComputeTriggerKind trigger) {
  if (target != ComputeTargetKind::Descriptor && descriptor != 0)
    return emitError() << "non-descriptor effect has a descriptor value";
  if (target != ComputeTargetKind::Formal && formal != 0)
    return emitError() << "non-formal effect has a formal index";
  if (resource == ComputeResourceKind::Unknown &&
      target != ComputeTargetKind::Unknown)
    return emitError() << "unknown effect has a concrete target";
  if (resource == ComputeResourceKind::Local &&
      target != ComputeTargetKind::Local)
    return emitError() << "local effect has a non-local target";
  if (resource != ComputeResourceKind::Unknown &&
      resource != ComputeResourceKind::Local &&
      target != ComputeTargetKind::Descriptor &&
      target != ComputeTargetKind::Formal)
    return emitError() << "concrete effect has no descriptor or formal target";
  if (resource == ComputeResourceKind::Unknown &&
      (low != 0 || width != 0 || dynamic))
    return emitError() << "unknown effect must not claim a concrete range";
  if (resource != ComputeResourceKind::Unknown && width == 0)
    return emitError() << "concrete effect has zero width";
  if (width != 0 && low > UINT64_MAX - width)
    return emitError() << "effect packed range overflows";
  bool watches = effect == ComputeEffectKind::Watch;
  if (watches != (trigger != ComputeTriggerKind::None))
    return emitError() << "watch effects require exactly one trigger kind";
  if (deferred && effect != ComputeEffectKind::NBA &&
      effect != ComputeEffectKind::Trigger)
    return emitError() << "only NBA and trigger effects may be deferred";
  return success();
}

LogicalResult
DPIABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   DPIABIKind kind, DPIArgumentDirection, uint32_t width,
                   bool fourState, bool isSigned) {
  if (width == 0)
    return emitError() << "DPI ABI width must be nonzero";
  auto require = [&](uint32_t expectedWidth,
                     bool expectedFourState) -> LogicalResult {
    if (width != expectedWidth)
      return emitError() << "DPI ABI category requires width " << expectedWidth;
    if (fourState != expectedFourState)
      return emitError() << "DPI ABI category has incompatible state kind";
    return success();
  };
  switch (kind) {
  case DPIABIKind::Bit:
    return require(1, false);
  case DPIABIKind::Logic:
    return require(1, true);
  case DPIABIKind::Byte:
    return require(8, false);
  case DPIABIKind::ShortInt:
    return require(16, false);
  case DPIABIKind::Int:
    return require(32, false);
  case DPIABIKind::LongInt:
    return require(64, false);
  case DPIABIKind::BitVector:
    return fourState
               ? emitError() << "bit-vector DPI ABI category must be two-state"
               : success();
  case DPIABIKind::LogicVector:
    return !fourState
               ? emitError()
                     << "logic-vector DPI ABI category must be four-state"
               : success();
  case DPIABIKind::String:
  case DPIABIKind::Chandle:
    if (isSigned)
      return emitError() << "DPI handle category cannot be signed";
    return require(64, false);
  }
  llvm_unreachable("unknown DPI ABI category");
}

uint64_t getDPISignatureHash(ArrayAttr signature, uint64_t logicalInputs) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  hash = obelisk_stable_hash_append_uint_le(hash, logicalInputs, 8);
  hash = obelisk_stable_hash_append_uint_le(
      hash, signature.size() - logicalInputs, 8);
  for (Attribute attribute : signature) {
    auto abi = cast<DPIABIAttr>(attribute);
    hash = obelisk_stable_hash_append_uint_le(
        hash, static_cast<uint32_t>(abi.getKind()), 4);
    hash = obelisk_stable_hash_append_uint_le(
        hash, static_cast<uint32_t>(abi.getDirection()), 4);
    hash = obelisk_stable_hash_append_uint_le(hash, abi.getWidth(), 4);
    hash =
        obelisk_stable_hash_append_uint_le(hash, abi.getFourState() ? 1 : 0, 1);
    hash =
        obelisk_stable_hash_append_uint_le(hash, abi.getIsSigned() ? 1 : 0, 1);
  }
  return hash ? hash : 1;
}

LogicalResult ComputeFragmentAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    FlatSymbolRefAttr function, uint32_t block, ComputeRegionKind region,
    ComputeActionKind action, ComputeTierKind tier, uint64_t cost,
    uint32_t lane, bool twoState, ArrayAttr effects) {
  if (!function)
    return emitError() << "fragment requires a function symbol";
  return verifyEffectArray(emitError, effects, "fragment");
}

LogicalResult ComputeNBACommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr slots, DenseI64ArrayAttr accumulatorSites,
    DenseI64ArrayAttr frontierSites, ComputeEffectAttr effect) {
  if (!slots || !accumulatorSites || !frontierSites || !effect ||
      effect.getEffect() != ComputeEffectKind::Write)
    return emitError()
           << "NBA commit requires staging inventories and one write effect";
  if (slots.empty() && accumulatorSites.empty() && frontierSites.empty())
    return emitError() << "NBA commit requires at least one site";
  llvm::SmallDenseSet<int64_t> sites;
  for (DenseI64ArrayAttr inventory : {slots, accumulatorSites, frontierSites})
    for (int64_t site : inventory.asArrayRef())
      if (site < 0 || !sites.insert(site).second)
        return emitError() << "NBA commit has an invalid or duplicate site";
  return success();
}

LogicalResult ComputeEventCommitAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    DenseI64ArrayAttr sites, ComputeEffectAttr effect) {
  if (!sites || !effect || effect.getEffect() != ComputeEffectKind::Trigger ||
      !effect.getDeferred())
    return emitError()
           << "event commit requires sites and one deferred trigger effect";
  if (sites.empty())
    return emitError() << "event commit requires at least one site";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t site : sites.asArrayRef())
    if (site < 0 || !unique.insert(site).second)
      return emitError() << "event commit has an invalid or duplicate site";
  return success();
}

LogicalResult
ComputeEdgeAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t source, uint32_t target, ComputeEdgeKind kind,
                        ComputeEffectAttr resource) {
  bool needsResource = kind == ComputeEdgeKind::Sensitivity ||
                       kind == ComputeEdgeKind::NBAStage ||
                       kind == ComputeEdgeKind::NBAActivate ||
                       kind == ComputeEdgeKind::Conflict ||
                       kind == ComputeEdgeKind::DeferredStage ||
                       kind == ComputeEdgeKind::DeferredActivate;
  if (needsResource && !resource)
    return emitError() << "edge kind requires a resource effect";
  if ((kind == ComputeEdgeKind::ProcessOrder ||
       kind == ComputeEdgeKind::Resume || kind == ComputeEdgeKind::Spawn) &&
      resource)
    return emitError() << "control-only edge cannot carry a resource";
  return success();
}

LogicalResult
ComputeGroupAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         DenseI64ArrayAttr fragments,
                         ComputeScheduleKind schedule, ArrayAttr feedback) {
  if (!fragments || fragments.empty() || !feedback)
    return emitError() << "schedule group must contain fragments and feedback";
  llvm::SmallDenseSet<int64_t> members;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !members.insert(fragment).second)
      return emitError() << "schedule group has an invalid or duplicate member";
  if (failed(verifyEffectArray(emitError, feedback, "schedule feedback")))
    return failure();
  if (schedule != ComputeScheduleKind::Convergence && !feedback.empty())
    return emitError() << "only convergence groups may carry feedback";
  return success();
}

LogicalResult
ComputeFusionAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint32_t id, DenseI64ArrayAttr fragments) {
  (void)id;
  if (!fragments || fragments.size() < 2)
    return emitError() << "compute fusion requires at least two fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "compute fusion has an invalid or duplicate fragment";
  return success();
}

LogicalResult ComputeKernelAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    ComputeRegionKind region, ComputeScheduleKind schedule, uint32_t lane,
    uint64_t cost, bool loweringReady, DenseI64ArrayAttr fragments) {
  (void)id;
  (void)region;
  (void)lane;
  (void)cost;
  if (!fragments || fragments.empty())
    return emitError() << "compute kernel must contain fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "compute kernel has an invalid or duplicate fragment";
  if (loweringReady && schedule == ComputeScheduleKind::ControlLoop)
    return emitError() << "control-loop kernel cannot be lowering-ready";
  return success();
}

LogicalResult
PhysicalTriggerAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            ComputeResourceKind resource, uint64_t descriptor,
                            uint64_t low, uint64_t width,
                            ComputeTriggerKind edge) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "physical trigger requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "physical trigger has an invalid packed range";
  if (edge != ComputeTriggerKind::Change &&
      edge != ComputeTriggerKind::Posedge &&
      edge != ComputeTriggerKind::Negedge && edge != ComputeTriggerKind::Both)
    return emitError() << "physical trigger requires an exact edge kind";
  return success();
}

LogicalResult
TriggerGroupAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         uint32_t id, PhysicalTriggerAttr key) {
  (void)id;
  if (!key)
    return emitError() << "trigger group has no physical key";
  return success();
}

LogicalResult
InductiveRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeResourceKind resource, uint64_t descriptor) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "inductive root requires storage or net state";
  return success();
}

LogicalResult ScheduledKernelAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t id,
    uint32_t owner, uint32_t readyBit, SchedulerTierKind tier,
    ComputeRegionKind region, ComputeScheduleKind schedule, bool shared,
    bool loweringReady, bool twoStateEligible, ArrayAttr promotionRoots,
    DenseI64ArrayAttr fragments) {
  (void)id;
  (void)owner;
  (void)readyBit;
  (void)region;
  (void)shared;
  if (!fragments || fragments.empty())
    return emitError() << "scheduled kernel must retain original fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "scheduled kernel has an invalid or duplicate fragment";
  if (tier == SchedulerTierKind::Tier1 &&
      schedule != ComputeScheduleKind::Acyclic)
    return emitError() << "Tier-1 kernel must be acyclic";
  if (tier == SchedulerTierKind::Tier2 &&
      schedule == ComputeScheduleKind::ControlLoop)
    return emitError() << "control loops require Tier 3";
  if (tier == SchedulerTierKind::Tier3 && loweringReady)
    return emitError() << "Tier-3 kernel cannot be lowering-ready";
  if (tier != SchedulerTierKind::Tier3 && !loweringReady)
    return emitError() << "generated kernel must be lowering-ready";
  if (twoStateEligible && (tier != SchedulerTierKind::Tier1 || !loweringReady))
    return emitError()
           << "only lowering-ready Tier-1 kernels may have two-state bodies";
  if (!promotionRoots)
    return emitError() << "scheduled kernel has no promotion-root inventory";
  llvm::SmallDenseSet<Attribute, 8> roots;
  for (Attribute root : promotionRoots)
    if (!isa<InductiveRootAttr>(root) || !roots.insert(root).second)
      return emitError() << "promotion-root inventory is invalid or duplicated";
  if (!twoStateEligible && !promotionRoots.empty())
    return emitError()
           << "kernel without a two-state body cannot have promotion roots";
  return success();
}

LogicalResult
SchedulerIngressAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                             uint32_t trigger, uint32_t owner,
                             uint32_t readyBit, uint32_t fragment) {
  (void)emitError;
  (void)trigger;
  (void)owner;
  (void)readyBit;
  (void)fragment;
  return success();
}

LogicalResult
ScheduledRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeResourceKind resource, uint64_t descriptor,
                          uint64_t low, uint64_t width, uint32_t owner,
                          SchedulerTierKind tier) {
  (void)descriptor;
  (void)owner;
  (void)tier;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "scheduled root requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "scheduled root has an invalid packed range";
  return success();
}

LogicalResult
ClockKeyAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     ComputeResourceKind resource, uint64_t descriptor,
                     uint64_t low, uint64_t width, ComputeTriggerKind edge) {
  (void)descriptor;
  if (resource != ComputeResourceKind::Storage &&
      resource != ComputeResourceKind::Net)
    return emitError() << "clock key requires storage or net state";
  if (width == 0 || low > UINT64_MAX - width)
    return emitError() << "clock key has an invalid packed range";
  if (edge != ComputeTriggerKind::Change &&
      edge != ComputeTriggerKind::Posedge &&
      edge != ComputeTriggerKind::Negedge && edge != ComputeTriggerKind::Both)
    return emitError() << "clock key requires an exact change or edge kind";
  return success();
}

LogicalResult
ClockKernelAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t id, ClockKeyAttr key) {
  (void)id;
  if (!key)
    return emitError() << "clock kernel has no physical key";
  return success();
}

LogicalResult
MergedFragmentAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           uint32_t id, uint32_t owner, uint32_t bit,
                           bool shared, bool loweringReady,
                           DenseI64ArrayAttr fragments) {
  (void)id;
  (void)owner;
  (void)bit;
  (void)shared;
  (void)loweringReady;
  if (!fragments || fragments.empty())
    return emitError() << "merged fragment must retain original fragments";
  llvm::SmallDenseSet<int64_t> unique;
  for (int64_t fragment : fragments.asArrayRef())
    if (fragment < 0 || !unique.insert(fragment).second)
      return emitError()
             << "merged fragment has an invalid or duplicate original ID";
  return success();
}

LogicalResult
KernelIngressAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint32_t clock, uint32_t owner, uint32_t bit,
                          uint32_t fragment) {
  (void)clock;
  (void)owner;
  (void)bit;
  (void)fragment;
  return success();
}

LogicalResult
ClockKernelPlanAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint32_t version, ComputeGraphAttr sourceGraph,
                            uint32_t ownerCount, ArrayAttr clocks,
                            ArrayAttr mergedFragments, ArrayAttr ingress) {
  if (version != metadata::schemaVersion || !sourceGraph)
    return emitError() << "invalid clock-kernel plan version or source graph";
  if (!clocks || !mergedFragments || !ingress)
    return emitError() << "clock-kernel plan inventory is absent";
  if (ownerCount < clocks.size())
    return emitError() << "clock-kernel plan has fewer owners than clocks";

  llvm::SmallDenseSet<Attribute, 16> keys;
  for (auto [index, attribute] : llvm::enumerate(clocks)) {
    auto clock = dyn_cast<ClockKernelAttr>(attribute);
    if (!clock || clock.getId() != index || !keys.insert(clock.getKey()).second)
      return emitError() << "clock-kernel inventory is invalid or duplicated";
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ownerBits;
  llvm::SmallDenseSet<int64_t, 32> originalFragments;
  for (auto [index, attribute] : llvm::enumerate(mergedFragments)) {
    auto merged = dyn_cast<MergedFragmentAttr>(attribute);
    if (!merged || merged.getId() != index || merged.getOwner() >= ownerCount ||
        !ownerBits.insert({merged.getOwner(), merged.getBit()}).second)
      return emitError()
             << "merged-fragment ownership is invalid or duplicated";
    if (merged.getShared() != (merged.getOwner() >= clocks.size()))
      return emitError() << "merged-fragment shared ownership is inconsistent";
    for (int64_t fragment : merged.getFragments().asArrayRef())
      if (static_cast<uint64_t>(fragment) >= sourceGraph.getNodes().size() ||
          !originalFragments.insert(fragment).second)
        return emitError()
               << "original fragment has no unique merged-fragment owner";
  }
  if (originalFragments.size() != sourceGraph.getNodes().size())
    return emitError() << "clock-kernel plan does not own every graph node";

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ingressKeys;
  for (Attribute attribute : ingress) {
    auto mapping = dyn_cast<KernelIngressAttr>(attribute);
    if (!mapping || mapping.getClock() >= clocks.size() ||
        mapping.getOwner() >= ownerCount ||
        !ownerBits.contains({mapping.getOwner(), mapping.getBit()}) ||
        mapping.getFragment() >= sourceGraph.getNodes().size() ||
        !ingressKeys.insert({mapping.getClock(), mapping.getFragment()}).second)
      return emitError()
             << "clock-kernel ingress mapping is invalid or duplicated";
  }
  return success();
}

LogicalResult
ComputeRegionAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          ComputeRegionKind kind, ArrayAttr groups) {
  if (!groups || llvm::any_of(groups, [](Attribute attr) {
        return !isa<ComputeGroupAttr>(attr);
      }))
    return emitError() << "event region contains a non-group attribute";
  return success();
}

LogicalResult
ComputeGraphAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         uint32_t version, ComputeVPIMode vpi, uint32_t workers,
                         ArrayAttr nodes, ArrayAttr edges, ArrayAttr regions) {
  if (version != metadata::schemaVersion)
    return emitError() << "unsupported compute-graph version";
  if (workers == 0 || workers > 65535)
    return emitError() << "worker count is outside the lane ID range";
  if (!nodes || llvm::any_of(nodes, [](Attribute attr) {
        return !isa<ComputeFragmentAttr, ComputeNBACommitAttr,
                    ComputeEventCommitAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-node attribute";
  if (!edges || llvm::any_of(edges, [](Attribute attr) {
        return !isa<ComputeEdgeAttr>(attr);
      }))
    return emitError() << "compute graph contains a non-edge attribute";
  if (!regions || regions.size() != 5)
    return emitError() << "compute graph requires all five event regions";
  static constexpr ComputeRegionKind expectedRegions[] = {
      ComputeRegionKind::Active, ComputeRegionKind::NBA,
      ComputeRegionKind::Observed, ComputeRegionKind::Reactive,
      ComputeRegionKind::Postponed};
  for (auto [attribute, expected] : llvm::zip(regions, expectedRegions)) {
    auto region = dyn_cast<ComputeRegionAttr>(attribute);
    if (!region || region.getKind() != expected)
      return emitError() << "compute graph event regions are out of order";
  }
  return success();
}

LogicalResult ThreeTierScheduleAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t version,
    ComputeGraphAttr sourceGraph, uint32_t ownerCount, ArrayAttr triggers,
    ArrayAttr kernels, ArrayAttr roots, ArrayAttr ingress) {
  if (version != metadata::schemaVersion || !sourceGraph)
    return emitError() << "invalid three-tier schedule version or source graph";
  if (!triggers || !kernels || !roots || !ingress)
    return emitError() << "three-tier schedule inventory is absent";
  if (ownerCount < triggers.size())
    return emitError() << "three-tier schedule has fewer owners than triggers";

  llvm::SmallDenseSet<Attribute, 16> keys;
  for (auto [index, attribute] : llvm::enumerate(triggers)) {
    auto trigger = dyn_cast<TriggerGroupAttr>(attribute);
    if (!trigger || trigger.getId() != index ||
        !keys.insert(trigger.getKey()).second)
      return emitError() << "trigger-group inventory is invalid or duplicated";
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ownerBits;
  llvm::SmallDenseSet<int64_t, 32> originalFragments;
  llvm::DenseMap<uint32_t, std::pair<uint32_t, uint32_t>> fragmentOwners;
  llvm::DenseMap<uint32_t, ScheduledKernelAttr> fragmentKernels;
  uint32_t nextSharedOwner = static_cast<uint32_t>(triggers.size());
  for (auto [index, attribute] : llvm::enumerate(kernels)) {
    auto kernel = dyn_cast<ScheduledKernelAttr>(attribute);
    if (!kernel || kernel.getId() != index || kernel.getOwner() >= ownerCount ||
        !ownerBits.insert({kernel.getOwner(), kernel.getReadyBit()}).second)
      return emitError() << "kernel ownership is invalid or duplicated";
    if (kernel.getShared() != (kernel.getOwner() >= triggers.size()))
      return emitError() << "kernel shared ownership is inconsistent";
    if (kernel.getShared() && kernel.getOwner() != nextSharedOwner++)
      return emitError() << "shared kernel owners are not canonical";
    if (!kernel.getShared() && kernel.getTier() != SchedulerTierKind::Tier1)
      return emitError() << "physical-trigger owners must remain in Tier 1";
    for (int64_t fragment : kernel.getFragments().asArrayRef()) {
      if (static_cast<uint64_t>(fragment) >= sourceGraph.getNodes().size() ||
          !originalFragments.insert(fragment).second)
        return emitError() << "graph node has no unique scheduled-kernel owner";
      fragmentOwners.try_emplace(static_cast<uint32_t>(fragment),
                                 kernel.getOwner(), kernel.getReadyBit());
      fragmentKernels.try_emplace(static_cast<uint32_t>(fragment), kernel);
    }
  }
  if (originalFragments.size() != sourceGraph.getNodes().size())
    return emitError() << "three-tier schedule does not own every graph node";

  llvm::SmallDenseSet<
      std::tuple<ComputeResourceKind, uint64_t, uint64_t, uint64_t>, 16>
      rootKeys;
  SmallVector<ScheduledRootAttr> scheduledRoots;
  for (Attribute attribute : roots) {
    auto root = dyn_cast<ScheduledRootAttr>(attribute);
    if (!root || root.getOwner() >= ownerCount ||
        !rootKeys
             .insert({root.getResource(), root.getDescriptor(), root.getLow(),
                      root.getWidth()})
             .second)
      return emitError() << "scheduled-root ownership is invalid or duplicated";
    scheduledRoots.push_back(root);
  }

  struct ScheduledWriter {
    ComputeResourceKind resource;
    uint64_t descriptor;
    uint64_t low;
    uint64_t high;
    uint32_t kernel;
    uint32_t owner;
    SchedulerTierKind tier;
  };
  SmallVector<ScheduledWriter> writers;
  auto recordWriter = [&](uint32_t fragmentID, ComputeEffectAttr effect) {
    if (!effect || effect.getTarget() != ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getWidth() == 0 ||
        (effect.getResource() != ComputeResourceKind::Storage &&
         effect.getResource() != ComputeResourceKind::Net) ||
        (effect.getEffect() != ComputeEffectKind::Write &&
         effect.getEffect() != ComputeEffectKind::Drive &&
         effect.getEffect() != ComputeEffectKind::NBA))
      return;
    auto kernel = fragmentKernels.find(fragmentID);
    if (kernel == fragmentKernels.end())
      return;
    writers.push_back({effect.getResource(), effect.getDescriptor(),
                       effect.getLow(), effect.getLow() + effect.getWidth(),
                       kernel->second.getId(), kernel->second.getOwner(),
                       kernel->second.getTier()});
  };
  for (auto [fragmentID, node] : llvm::enumerate(sourceGraph.getNodes())) {
    if (auto fragment = dyn_cast<ComputeFragmentAttr>(node))
      for (Attribute effect : fragment.getEffects())
        recordWriter(static_cast<uint32_t>(fragmentID),
                     cast<ComputeEffectAttr>(effect));
    else if (auto commit = dyn_cast<ComputeNBACommitAttr>(node))
      recordWriter(static_cast<uint32_t>(fragmentID), commit.getEffect());
  }
  llvm::sort(scheduledRoots, [](ScheduledRootAttr lhs, ScheduledRootAttr rhs) {
    return std::tuple(lhs.getResource(), lhs.getDescriptor(), lhs.getLow(),
                      lhs.getWidth()) <
           std::tuple(rhs.getResource(), rhs.getDescriptor(), rhs.getLow(),
                      rhs.getWidth());
  });
  using DescriptorKey = std::pair<ComputeResourceKind, uint64_t>;
  std::map<DescriptorKey, std::map<std::vector<uint32_t>, uint32_t>>
      barrierOwners;
  uint32_t nextBarrierOwner = nextSharedOwner;
  for (ScheduledRootAttr root : scheduledRoots) {
    uint64_t high = root.getLow() + root.getWidth();
    SmallVector<const ScheduledWriter *> overlapping;
    for (const ScheduledWriter &writer : writers)
      if (writer.resource == root.getResource() &&
          writer.descriptor == root.getDescriptor() && writer.low < high &&
          root.getLow() < writer.high)
        overlapping.push_back(&writer);
    if (overlapping.empty() ||
        llvm::any_of(overlapping, [&](const ScheduledWriter *writer) {
          return writer->low > root.getLow() || writer->high < high;
        }))
      return emitError()
             << "scheduled root crosses or lacks an exact writer partition";
    SchedulerTierKind tier = overlapping.front()->tier;
    uint32_t commonOwner = overlapping.front()->owner;
    bool common = true;
    SmallVector<uint32_t> writerKernels;
    writerKernels.push_back(overlapping.front()->kernel);
    for (const ScheduledWriter *writer : ArrayRef(overlapping).drop_front()) {
      if (static_cast<uint32_t>(writer->tier) > static_cast<uint32_t>(tier))
        tier = writer->tier;
      common &= writer->owner == commonOwner;
      writerKernels.push_back(writer->kernel);
    }
    uint32_t expectedOwner = commonOwner;
    if (!common) {
      llvm::sort(writerKernels);
      writerKernels.erase(
          std::unique(writerKernels.begin(), writerKernels.end()),
          writerKernels.end());
      std::vector<uint32_t> writerKey(writerKernels.begin(),
                                      writerKernels.end());
      auto &descriptorOwners =
          barrierOwners[{root.getResource(), root.getDescriptor()}];
      auto [barrier, inserted] =
          descriptorOwners.try_emplace(std::move(writerKey), nextBarrierOwner);
      if (inserted)
        ++nextBarrierOwner;
      expectedOwner = barrier->second;
    }
    if (root.getTier() != tier || root.getOwner() != expectedOwner)
      return emitError()
             << "scheduled-root owner or tier disagrees with writers";
  }
  if (nextBarrierOwner != ownerCount)
    return emitError() << "three-tier owner inventory is not canonical";
  for (const ScheduledWriter &writer : writers) {
    SmallVector<std::pair<uint64_t, uint64_t>> coverage;
    for (ScheduledRootAttr root : scheduledRoots)
      if (root.getResource() == writer.resource &&
          root.getDescriptor() == writer.descriptor &&
          root.getLow() < writer.high &&
          writer.low < root.getLow() + root.getWidth())
        coverage.push_back({root.getLow(), root.getLow() + root.getWidth()});
    llvm::sort(coverage);
    uint64_t cursor = writer.low;
    for (auto [low, high] : coverage) {
      if (low > cursor)
        break;
      cursor = std::max(cursor, high);
    }
    if (cursor < writer.high)
      return emitError() << "scheduled roots do not cover every writer range";
  }

  auto isPhysicalWatch = [](ComputeEffectAttr effect) {
    return effect.getEffect() == ComputeEffectKind::Watch &&
           effect.getTarget() == ComputeTargetKind::Descriptor &&
           !effect.getDynamic() && !effect.getDeferred() &&
           effect.getWidth() != 0 &&
           (effect.getResource() == ComputeResourceKind::Storage ||
            effect.getResource() == ComputeResourceKind::Net) &&
           (effect.getTrigger() == ComputeTriggerKind::Change ||
            effect.getTrigger() == ComputeTriggerKind::Posedge ||
            effect.getTrigger() == ComputeTriggerKind::Negedge ||
            effect.getTrigger() == ComputeTriggerKind::Both);
  };
  auto findTrigger = [&](ComputeEffectAttr effect) -> std::optional<uint32_t> {
    for (auto [index, attribute] : llvm::enumerate(triggers)) {
      auto key = cast<TriggerGroupAttr>(attribute).getKey();
      if (key.getResource() == effect.getResource() &&
          key.getDescriptor() == effect.getDescriptor() &&
          key.getLow() == effect.getLow() &&
          key.getWidth() == effect.getWidth() &&
          key.getEdge() == effect.getTrigger())
        return static_cast<uint32_t>(index);
    }
    return std::nullopt;
  };

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> requiredIngress;
  for (auto [fragmentID, node] : llvm::enumerate(sourceGraph.getNodes())) {
    auto fragment = dyn_cast<ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<ComputeEffectAttr>(effectAttribute);
      if (!isPhysicalWatch(effect))
        continue;
      std::optional<uint32_t> trigger = findTrigger(effect);
      if (!trigger)
        return emitError()
               << "physical watch is absent from the trigger inventory";
      requiredIngress.insert({*trigger, static_cast<uint32_t>(fragmentID)});
    }
  }

  llvm::SmallDenseSet<std::pair<uint32_t, uint32_t>, 16> ingressKeys;
  for (Attribute attribute : ingress) {
    auto mapping = dyn_cast<SchedulerIngressAttr>(attribute);
    auto owner = mapping ? fragmentOwners.find(mapping.getFragment())
                         : fragmentOwners.end();
    if (!mapping || mapping.getTrigger() >= triggers.size() ||
        mapping.getOwner() >= ownerCount ||
        !ownerBits.contains({mapping.getOwner(), mapping.getReadyBit()}) ||
        mapping.getFragment() >= sourceGraph.getNodes().size() ||
        owner == fragmentOwners.end() ||
        owner->second != std::pair(mapping.getOwner(), mapping.getReadyBit()) ||
        !requiredIngress.contains(
            {mapping.getTrigger(), mapping.getFragment()}) ||
        !ingressKeys.insert({mapping.getTrigger(), mapping.getFragment()})
             .second)
      return emitError() << "scheduler ingress is invalid or duplicated";
  }
  if (ingressKeys.size() != requiredIngress.size())
    return emitError()
           << "scheduler ingress does not cover every physical watch";
  return success();
}

LogicalResult
StaticStateRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint64_t descriptor, uint32_t width, bool direct,
                            bool guarded, bool nba) {
  (void)descriptor;
  if (direct && guarded)
    return emitError() << "static state root cannot be both direct and guarded";
  if ((direct || guarded || nba) && width == 0)
    return emitError() << "specialized static state root has zero width";
  return success();
}

LogicalResult
StaticActorRootAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            FlatSymbolRefAttr function, uint64_t descriptor,
                            bool read, bool write) {
  (void)descriptor;
  if (!function)
    return emitError() << "static actor/root dependency has no actor";
  if (!read && !write)
    return emitError() << "static actor/root dependency has no access kind";
  return success();
}

LogicalResult StaticSpecializationAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t version,
    uint32_t maxPackedWidth, ComputeGraphAttr sourceGraph, ArrayAttr roots,
    ArrayAttr actorRoots, DenseI64ArrayAttr nbaRoots) {
  if (version != metadata::schemaVersion || maxPackedWidth == 0 ||
      maxPackedWidth > metadata::maxDirectStaticStateBits || !sourceGraph)
    return emitError() << "invalid static-specialization version or width";
  if (!roots || !actorRoots || !nbaRoots)
    return emitError() << "static-specialization inventory is absent";

  DenseMap<uint64_t, StaticStateRootAttr> rootByDescriptor;
  for (Attribute attribute : roots) {
    auto root = dyn_cast<StaticStateRootAttr>(attribute);
    if (!root)
      return emitError()
             << "static-specialization root array has an invalid element";
    if (!rootByDescriptor.try_emplace(root.getDescriptor(), root).second)
      return emitError() << "static-specialization root is duplicated";
  }

  llvm::SmallDenseSet<std::pair<Attribute, uint64_t>, 16> dependencies;
  for (Attribute attribute : actorRoots) {
    auto dependency = dyn_cast<StaticActorRootAttr>(attribute);
    if (!dependency)
      return emitError() << "static actor/root array has an invalid element";
    auto root = rootByDescriptor.find(dependency.getDescriptor());
    if (root == rootByDescriptor.end() ||
        (!root->second.getDirect() && !root->second.getGuarded()))
      return emitError()
             << "static actor/root dependency references a generic root";
    if (!dependencies
             .insert({dependency.getFunction(), dependency.getDescriptor()})
             .second)
      return emitError() << "static actor/root dependency is duplicated";
  }

  llvm::SmallDenseSet<uint64_t, 16> orderedNBARoots;
  for (int64_t descriptor : nbaRoots.asArrayRef()) {
    if (descriptor < 0 ||
        !orderedNBARoots.insert(static_cast<uint64_t>(descriptor)).second)
      return emitError()
             << "static NBA root inventory has an invalid or duplicate root";
    auto root = rootByDescriptor.find(static_cast<uint64_t>(descriptor));
    if (root == rootByDescriptor.end() || !root->second.getNba())
      return emitError()
             << "static NBA root inventory references a generic root";
  }
  for (const auto &entry : rootByDescriptor)
    if (entry.second.getNba() && !orderedNBARoots.contains(entry.first))
      return emitError()
             << "static NBA root policy is absent from the ordered inventory";
  return success();
}

LogicalResult
StaticSuperstepAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                            uint32_t version, ComputeGraphAttr sourceGraph,
                            ArrayAttr actors) {
  if (version != metadata::schemaVersion || !sourceGraph ||
      sourceGraph.getWorkers() != 1)
    return emitError() << "invalid static-superstep version or worker count";
  if (!actors || actors.empty())
    return emitError() << "static-superstep actor inventory is absent";
  llvm::SmallDenseSet<Attribute, 16> uniqueActors;
  for (Attribute attribute : actors) {
    auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
    if (!actor || !uniqueActors.insert(actor).second)
      return emitError()
             << "static-superstep actor inventory is invalid or duplicated";
  }
  return success();
}

LogicalResult
FragmentABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        uint32_t version, DenseI64ArrayAttr fragments) {
  if (version != metadata::schemaVersion || !fragments)
    return emitError() << "invalid fragment ABI version or inventory";
  llvm::SmallDenseSet<int64_t> ids;
  for (int64_t id : fragments.asArrayRef())
    if (id < 0 || !ids.insert(id).second)
      return emitError() << "fragment ABI has an invalid or duplicate ID";
  return success();
}

LogicalResult
NBASiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                    uint64_t id, uint32_t commit, ComputeNBAStorageKind storage,
                    TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedNBA)
    return emitError() << "NBA timing site must have delayed_nba kind";
  return success();
}

LogicalResult
EventSiteAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                      uint64_t id, uint32_t commit, TimingSiteAttr timing) {
  if (timing && timing.getKind() != ComputeTimingKind::DelayedEvent)
    return emitError()
           << "deferred-event timing site must have delayed_event kind";
  return success();
}

std::optional<unsigned> getPackedWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless() ? std::optional<unsigned>(integer.getWidth())
                                : std::nullopt;
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  auto checkedProduct = [](uint64_t lhs,
                           uint64_t rhs) -> std::optional<unsigned> {
    if (lhs && rhs > std::numeric_limits<unsigned>::max() / lhs)
      return std::nullopt;
    uint64_t result = lhs * rhs;
    if (result == 0 || result > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(result);
  };
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    std::optional<unsigned> element = getPackedWidth(array.getElementType());
    std::optional<unsigned> count =
        getArrayElementOrdinal(array, array.getRight());
    if (!element || !count)
      return std::nullopt;
    return checkedProduct(static_cast<uint64_t>(*count) + 1, *element);
  }
  auto aggregateWidth = [&](ArrayAttr fields) -> std::optional<unsigned> {
    uint64_t width = 0;
    for (Attribute attribute : fields) {
      auto field = dyn_cast<FieldAttr>(attribute);
      std::optional<unsigned> fieldWidth =
          field ? getPackedWidth(field.getType()) : std::nullopt;
      if (!fieldWidth || field.getPackedOffset() >
                             std::numeric_limits<unsigned>::max() - *fieldWidth)
        return std::nullopt;
      width = std::max<uint64_t>(width, field.getPackedOffset() +
                                            static_cast<uint64_t>(*fieldWidth));
    }
    if (width == 0 || width > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(width);
  };
  if (auto structure = dyn_cast<PackedStructType>(type))
    return aggregateWidth(structure.getFields());
  if (auto unionType = dyn_cast<PackedUnionType>(type)) {
    std::optional<unsigned> payload = aggregateWidth(unionType.getFields());
    if (!payload || unionType.getTagBits() >
                        std::numeric_limits<unsigned>::max() - *payload)
      return std::nullopt;
    return *payload + unionType.getTagBits();
  }
  return std::nullopt;
}

} // namespace obelisk::sim
