//===- BuildComputeGraph.cpp - Attach the derived schedule ---------------===//
//
// The executable obelisk_sim CFG remains the source of truth. This pass runs
// the shared late analysis and writes its result onto the design as compiler
// metadata: descriptor-range summaries, fixed static sites, fragment ABI
// records, and the derived event-region graph.
//
//===----------------------------------------------------------------------===//

#include "ComputeGraph.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMBUILDCOMPUTEGRAPHPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimBuildComputeGraphPass
    : public impl::ObeliskSimBuildComputeGraphPassBase<
          ObeliskSimBuildComputeGraphPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

static bool isProceduralProcess(sim::EntryKind kind) {
  return kind == sim::EntryKind::Initial || kind == sim::EntryKind::Final ||
         kind == sim::EntryKind::Always ||
         kind == sim::EntryKind::AlwaysComb ||
         kind == sim::EntryKind::AlwaysFF ||
         kind == sim::EntryKind::AlwaysLatch ||
         kind == sim::EntryKind::Fork;
}

static LogicalResult verifyVariableWriters(
    sim::SimDesignOp design,
    const simlowering::ComputeGraphResult &derived) {
  llvm::MapVector<uint64_t, SmallVector<sim::SimFuncOp>> continuousWriters;
  llvm::MapVector<uint64_t, SmallVector<sim::SimFuncOp>> proceduralWriters;

  for (auto [operation, summary] : derived.effectSummaries) {
    auto function = cast<sim::SimFuncOp>(operation);
    bool continuous =
        function.getEntryKind() == sim::EntryKind::Continuous;
    bool procedural = isProceduralProcess(function.getEntryKind());
    if (!continuous && !procedural)
      continue;
    for (Attribute attribute : summary) {
      auto effect = cast<sim::ComputeEffectAttr>(attribute);
      if (effect.getResource() != sim::ComputeResourceKind::Storage ||
          effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
          (effect.getEffect() != sim::ComputeEffectKind::Write &&
           effect.getEffect() != sim::ComputeEffectKind::NBA))
        continue;
      auto &writers = continuous ? continuousWriters[effect.getDescriptor()]
                                 : proceduralWriters[effect.getDescriptor()];
      if (!llvm::is_contained(writers, function))
        writers.push_back(function);
    }
  }

  llvm::DenseMap<uint64_t, sim::SimStorageDeclOp> storageByID;
  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    storageByID[storage.getId()] = storage;
  auto storageName = [&](uint64_t descriptor) {
    sim::SimStorageDeclOp storage = storageByID.lookup(descriptor);
    if (storage && storage.getHierarchicalName())
      return storage.getHierarchicalName()->str();
    return (Twine("storage descriptor ") + Twine(descriptor)).str();
  };

  bool invalid = false;
  for (auto &[descriptor, continuous] : continuousWriters) {
    if (continuous.size() > 1) {
      continuous[1].emitOpError()
          << "variable '" << storageName(descriptor)
          << "' is driven by multiple continuous assignments";
      continuous.front().emitRemark("first continuous assignment is here");
      invalid = true;
    }
    auto procedural = proceduralWriters.find(descriptor);
    if (procedural != proceduralWriters.end() && !procedural->second.empty()) {
      procedural->second.front().emitOpError()
          << "variable '" << storageName(descriptor)
          << "' is written by both continuous and procedural assignments";
      continuous.front().emitRemark("continuous assignment is here");
      invalid = true;
    }
  }
  return failure(invalid);
}

void ObeliskSimBuildComputeGraphPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();

  simlowering::ComputeGraphOptions options;
  options.workers = workers;
  std::optional<sim::ComputeVPIMode> vpiMode =
      sim::symbolizeComputeVPIMode(vpi);
  if (!vpiMode) {
    design.emitOpError("VPI mode must be off, read, or full");
    return signalPassFailure();
  }
  options.vpi = *vpiMode;
  if (*vpiMode != sim::ComputeVPIMode::Off) {
    bool unsupportedContainer = false;
    for (sim::SimStorageDeclOp storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      bool dynamic = false;
      storage.getType().walk([&](Type type) {
        dynamic |= isa<sim::DynamicArrayType, sim::QueueType,
                       sim::AssocArrayType>(type);
      });
      if (!dynamic)
        continue;
      storage.emitOpError(
          "VPI dynamic-array, queue, and associative-array marshalling is "
          "unsupported");
      unsupportedContainer = true;
    }
    if (unsupportedContainer)
      return signalPassFailure();
  }

  FailureOr<simlowering::ComputeGraphResult> derived =
      simlowering::deriveComputeGraph(design, options);
  if (failed(derived))
    return signalPassFailure();
  if (failed(verifyVariableWriters(design, *derived)))
    return signalPassFailure();

  for (auto [function, summary] : derived->effectSummaries)
    cast<sim::SimFuncOp>(function).setEffectSummaryAttr(summary);
  for (auto [function, abi] : derived->fragmentAbis)
    cast<sim::SimFuncOp>(function).setFragmentAbiAttr(abi);
  for (auto [operation, site] : derived->continuations)
    simlowering::setContinuationSite(operation, site);
  for (auto [operation, timing] : derived->timings)
    cast<sim::SimSuspendDelayOp>(operation).setTimingAttr(timing);
  for (auto [operation, site] : derived->nbaSites)
    cast<sim::SimNBAEnqueueOp>(operation).setSiteAttr(site);
  for (auto [operation, site] : derived->eventSites)
    cast<sim::SimEventTriggerOp>(operation).setSiteAttr(site);

  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    storage.setObservability(derived->observability);
  for (sim::SimNetDeclOp net :
       design.getBody().front().getOps<sim::SimNetDeclOp>())
    net.setObservability(derived->observability);

  design.setComputeGraphAttr(derived->graph);
}

} // namespace
} // namespace obelisk
