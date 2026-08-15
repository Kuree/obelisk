//===- BuildComputeGraph.cpp - Attach the derived schedule ---------------===//
//
// The executable obelisk_sim CFG remains the source of truth. This pass runs
// the shared late analysis and writes its result onto the design as compiler
// metadata: descriptor-range summaries, fixed static sites, fragment ABI
// records, and the derived event-region graph.
//
//===----------------------------------------------------------------------===//

#include "ComputeGraph.h"

#include "obelisk/Analysis/SimulationVPIAnalysis.h"
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
  ObeliskSimBuildComputeGraphPass() = default;
  explicit ObeliskSimBuildComputeGraphPass(
      ObeliskSimBuildComputeGraphPassOptions options,
      bool reuseKnownCurrentGraph = false)
      : Base(std::move(options)),
        reuseKnownCurrentGraph(reuseKnownCurrentGraph) {}
  ObeliskSimBuildComputeGraphPass(const ObeliskSimBuildComputeGraphPass &other)
      : Base(other), reuseKnownCurrentGraph(other.reuseKnownCurrentGraph) {}
  void runOnOperation() override;

private:
  bool reuseKnownCurrentGraph = false;
};

static bool isProceduralProcess(sim::EntryKind kind) {
  return kind == sim::EntryKind::Initial || kind == sim::EntryKind::Final ||
         kind == sim::EntryKind::Always || kind == sim::EntryKind::AlwaysComb ||
         kind == sim::EntryKind::AlwaysFF ||
         kind == sim::EntryKind::AlwaysLatch || kind == sim::EntryKind::Fork;
}

/// Whether two writes to one descriptor can reach the same bits. Distinct
/// elements of an unpacked array and disjoint parts of a packed one are
/// separate drivers of the same variable, which IEEE 1800-2017 10.3.2 allows.
/// A dynamic range is unresolved, so it has to be assumed to cover everything.
static bool writesOverlap(sim::ComputeEffectAttr lhs,
                          sim::ComputeEffectAttr rhs) {
  if (lhs.getDynamic() || rhs.getDynamic())
    return true;
  constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
  if (lhs.getLow() > maximum - lhs.getWidth() ||
      rhs.getLow() > maximum - rhs.getWidth())
    return true;
  return lhs.getLow() < rhs.getLow() + rhs.getWidth() &&
         rhs.getLow() < lhs.getLow() + lhs.getWidth();
}

struct DescriptorWrite {
  sim::SimFuncOp function;
  sim::ComputeEffectAttr effect;
};

/// The first write in `candidates` from another process that reaches bits in
/// common with `write`. Callers pass the writes that precede `write`, so the
/// conflict is always reported on the later of the two.
static std::optional<DescriptorWrite>
findOverlap(ArrayRef<DescriptorWrite> candidates,
            const DescriptorWrite &write) {
  for (const DescriptorWrite &candidate : candidates)
    if (candidate.function != write.function &&
        writesOverlap(candidate.effect, write.effect))
      return candidate;
  return std::nullopt;
}

static LogicalResult
verifyVariableWriters(sim::SimDesignOp design,
                      const simlowering::ComputeGraphResult &derived) {
  llvm::MapVector<uint64_t, SmallVector<DescriptorWrite>> continuousWrites;
  llvm::MapVector<uint64_t, SmallVector<DescriptorWrite>> proceduralWrites;

  for (auto [operation, summary] : derived.effectSummaries) {
    auto function = cast<sim::SimFuncOp>(operation);
    bool continuous = function.getEntryKind() == sim::EntryKind::Continuous;
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
      auto &writes = continuous ? continuousWrites[effect.getDescriptor()]
                                : proceduralWrites[effect.getDescriptor()];
      writes.push_back({function, effect});
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
  for (auto &[descriptor, continuous] : continuousWrites) {
    for (auto [index, write] : llvm::enumerate(continuous)) {
      std::optional<DescriptorWrite> earlier =
          findOverlap(ArrayRef(continuous).take_front(index), write);
      if (!earlier)
        continue;
      write.function.emitOpError()
          << "variable '" << storageName(descriptor)
          << "' is driven by multiple continuous assignments";
      earlier->function.emitRemark("first continuous assignment is here");
      invalid = true;
      break;
    }
    auto procedural = proceduralWrites.find(descriptor);
    if (procedural == proceduralWrites.end())
      continue;
    for (DescriptorWrite &write : procedural->second) {
      std::optional<DescriptorWrite> conflict = findOverlap(continuous, write);
      if (!conflict)
        continue;
      write.function.emitOpError()
          << "variable '" << storageName(descriptor)
          << "' is written by both continuous and procedural assignments";
      conflict->function.emitRemark("continuous assignment is here");
      invalid = true;
      break;
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
  if (reuseKnownCurrentGraph) {
    sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (graph && graph.getWorkers() == options.workers &&
        graph.getVpi() == options.vpi) {
      if (failed(simlowering::validateComputeGraphStructure(design, graph)))
        signalPassFailure();
      return;
    }
  }
  if (analysis::SimulationVPIAnalysis::forMode(*vpiMode).allowsRead()) {
    bool unsupportedContainer = false;
    for (sim::SimStorageDeclOp storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      bool dynamic = false;
      storage.getType().walk([&](Type type) {
        dynamic |=
            isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                type);
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

std::unique_ptr<mlir::Pass> createObeliskSimBuildCurrentComputeGraphPass(
    ObeliskSimBuildComputeGraphPassOptions options) {
  return std::make_unique<ObeliskSimBuildComputeGraphPass>(std::move(options),
                                                           true);
}
} // namespace obelisk
