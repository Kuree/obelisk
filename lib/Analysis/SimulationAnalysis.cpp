//===- SimulationAnalysis.cpp - Shared simulation optimization facts -----===//

#include "talos/Analysis/SimulationAnalysis.h"
#include "talos/Dialect/Schedule/ScheduleAttrs.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"
#include "talos/Dialect/Schedule/ScheduleMetadata.h"
#include "talos/Dialect/Simulation/SimulationMetadata.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace talos::analysis {
std::optional<unsigned> getSimulationStorageBitWidth(Type type) {
  if (sim::isSimulationHandleType(type) || sim::isManagedHandleType(type))
    return sim::simulationHandleBitWidth;
  if (std::optional<unsigned> packed = sim::getPackedWidth(type))
    return packed;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type);
      unionType && unionType.getIsTagged() && span) {
    uint64_t tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
    if (tagBits > std::numeric_limits<uint64_t>::max() - *span)
      return std::nullopt;
    *span += tagBits;
  }
  if (!span || *span == 0 || *span > std::numeric_limits<unsigned>::max())
    return std::nullopt;
  return static_cast<unsigned>(*span);
}

bool containsFourStateLogic(Type type) {
  if (sim::isManagedHandleType(type))
    return false;
  bool result = false;
  type.walk([&](sim::LogicType) { result = true; });
  return result;
}

uint64_t getSimulationOperationCost(Operation &operation) {
  if (sim::has<sim::metadata::Rematerialized>(&operation) &&
      operation.hasTrait<OpTrait::ConstantLike>())
    return 0;
  if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimNetReadOp,
          sim::SimDriverDriveOp, sim::SimDriverDriveInertialOp,
          sim::SimDriverDriveInertialPathOp, sim::SimRefStoreInertialPathOp,
          sim::SimDriverDriveInertialStrengthPairOp,
          sim::SimDriverDriveInertialPathStrengthPairOp,
          sim::SimMosDriveDelayedOp, sim::SimDriverDriveDelayedNetOp,
          sim::SimDriverDriveChangedOp, sim::SimNBAEnqueueOp,
          sim::SimManagedNBAEnqueueOp, sim::SimManagedNBAEnqueueMaskedOp,
          sim::SimReferencePathNBAEnqueueOp>(operation))
    return 3;
  if (isa<sim::SimCallOp>(operation))
    return 5;
  if (isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
          sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
          sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
          sim::SimSuspendClockSetOp, sim::SimSuspendEventOp,
          sim::SimSuspendEventOrderOp, sim::SimSuspendMailboxOp,
          sim::SimSuspendSemaphoreOp, sim::SimSuspendForeverOp,
          sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp>(operation))
    return 1;
  return operation.hasTrait<OpTrait::IsTerminator>() ? 0 : 1;
}

uint64_t getSimulationOperationCost(Operation *operation) {
  uint64_t cost = 0;
  operation->walk([&](Operation *nested) {
    if (nested != operation)
      cost += getSimulationOperationCost(*nested);
  });
  return cost;
}

uint64_t getSimulationRegionCost(Region &region) {
  uint64_t cost = 0;
  region.walk([&](Operation *operation) {
    cost += getSimulationOperationCost(*operation);
  });
  return cost;
}

NBAMergeSafety::NBAMergeSafety(sim::SimDesignOp design) {
  auto observed = design
                      ? ::talos::schedule::get<
                            schedule::metadata::nbaTransientObservable>(design)
                      : DenseI64ArrayAttr{};
  schedule::ComputeGraphAttr graph =
      design ? design.getComputeGraphAttr() : schedule::ComputeGraphAttr{};
  if (!observed || !graph)
    return;
  known = true;
  if (auto roots = schedule::get<schedule::Field::NbaPrivateInfluence>(design))
    for (int64_t descriptor : roots.asArrayRef())
      privateInfluence.insert(static_cast<uint64_t>(descriptor));
  for (int64_t descriptor : observed.asArrayRef())
    observable.insert(static_cast<uint64_t>(descriptor));
  if (auto watched =
          ::talos::schedule::get<schedule::metadata::nbaChangeWatched>(
              design))
    for (int64_t descriptor : watched.asArrayRef())
      changeWatched.insert(static_cast<uint64_t>(descriptor));
  for (Attribute node : graph.getNodes()) {
    auto commit = dyn_cast<schedule::ComputeNBACommitAttr>(node);
    if (!commit)
      continue;
    schedule::ComputeEffectAttr effect = commit.getEffect();
    if (effect.getResource() == schedule::ComputeResourceKind::Storage &&
        effect.getTarget() == schedule::ComputeTargetKind::Descriptor &&
        !effect.getDynamic())
      commitStorage.try_emplace(commit.getId(), effect.getDescriptor());
  }
}

} // namespace talos::analysis
