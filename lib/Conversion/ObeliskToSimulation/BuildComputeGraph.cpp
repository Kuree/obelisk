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
        dynamic |= isa<sim::DynamicArrayType, sim::QueueType>(type);
      });
      if (!dynamic)
        continue;
      storage.emitOpError(
          "VPI dynamic-array and queue marshalling is unsupported");
      unsupportedContainer = true;
    }
    if (unsupportedContainer)
      return signalPassFailure();
  }

  FailureOr<simlowering::ComputeGraphResult> derived =
      simlowering::deriveComputeGraph(design, options);
  if (failed(derived))
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
