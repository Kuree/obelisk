//===- FuseComputeFragments.cpp - Plan static AOT execution batches ------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFUSECOMPUTEFRAGMENTSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimFuseComputeFragmentsPass final
    : public impl::ObeliskSimFuseComputeFragmentsPassBase<
          ObeliskSimFuseComputeFragmentsPass> {
public:
  void runOnOperation() override;
};

struct FusionCandidate {
  int64_t fragment;
  uint32_t order;
  Operation *function;
};

void ObeliskSimFuseComputeFragmentsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  design->removeAttr(sim::metadata::staticFusion);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph || graph.getWorkers() != 1 ||
      graph.getVpi() != sim::ComputeVPIMode::Off)
    return;

  ArrayAttr nodes = graph.getNodes();
  llvm::SmallDenseSet<int64_t> acyclicActive;
  DenseMap<int64_t, uint32_t> scheduleOrder;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      for (int64_t member : group.getFragments().asArrayRef()) {
        scheduleOrder.try_emplace(member, nextOrder++);
        if (group.getSchedule() == sim::ComputeScheduleKind::Acyclic)
          acyclicActive.insert(member);
      }
    }
  }

  llvm::MapVector<Attribute, SmallVector<FusionCandidate>> bySensitivity;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment || fragment.getId() != index ||
        !acyclicActive.contains(static_cast<int64_t>(index)) ||
        fragment.getTier() != sim::ComputeTierKind::Native ||
        fragment.getRegion() != sim::ComputeRegionKind::Active ||
        (fragment.getAction() != sim::ComputeActionKind::SuspendChange &&
         fragment.getAction() != sim::ComputeActionKind::SuspendEdge))
      continue;

    sim::ComputeEffectAttr sensitivity;
    bool unsupported = false;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() != sim::ComputeEffectKind::Watch)
        continue;
      if (sensitivity) {
        unsupported = true;
        break;
      }
      sensitivity = effect;
    }
    if (unsupported || !sensitivity ||
        sensitivity.getTarget() != sim::ComputeTargetKind::Descriptor ||
        (sensitivity.getResource() != sim::ComputeResourceKind::Storage &&
         sensitivity.getResource() != sim::ComputeResourceKind::Net) ||
        sensitivity.getDynamic() || sensitivity.getDeferred() ||
        sensitivity.getWidth() == 0)
      continue;

    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    if (!function)
      continue;
    bool controlSensitive = false;
    function.walk([&](Operation *operation) {
      controlSensitive |=
          isa<sim::SimStopOp, sim::SimFatalOp, sim::SimDPICallOp,
              sim::SimOverrideOp, sim::SimReleaseOverrideOp, sim::SimTaskCallOp,
              sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
              sim::SimSuspendChildrenOp>(operation);
    });
    if (controlSensitive)
      continue;

    bySensitivity[sensitivity].push_back({static_cast<int64_t>(index),
                                          scheduleOrder.lookup(index),
                                          function.getOperation()});
  }

  SmallVector<Attribute> fusions;
  uint32_t id = 0;
  for (auto &[sensitivity, candidates] : bySensitivity) {
    (void)sensitivity;
    if (candidates.size() < 2)
      continue;
    llvm::sort(candidates,
               [](const FusionCandidate &lhs, const FusionCandidate &rhs) {
                 return std::tie(lhs.order, lhs.fragment) <
                        std::tie(rhs.order, rhs.fragment);
               });
    llvm::SmallDenseSet<Operation *> functions;
    SmallVector<int64_t> fragments;
    for (const FusionCandidate &candidate : candidates)
      if (functions.insert(candidate.function).second)
        fragments.push_back(candidate.fragment);
    if (fragments.size() < 2)
      continue;
    fusions.push_back(sim::ComputeFusionAttr::get(
        design.getContext(), id++,
        DenseI64ArrayAttr::get(design.getContext(), fragments)));
  }
  if (!fusions.empty())
    design->setAttr(sim::metadata::staticFusion,
                    ArrayAttr::get(design.getContext(), fusions));
}

} // namespace
} // namespace obelisk
