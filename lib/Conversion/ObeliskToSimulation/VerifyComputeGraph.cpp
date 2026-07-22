//===- VerifyComputeGraph.cpp - Validate late schedule metadata ----------===//
//
// The compute graph is derived metadata. This pass first validates its local
// and cross-attribute representation invariants independently, then re-derives
// the whole schedule with the options the graph records and compares it with
// the executable CFG. That also makes it usable on reloaded simulation IR.
//
// It deliberately does not reimplement the analysis: a second hand-written
// copy of the alias and staging rules would drift silently rather than fail.
//
//===----------------------------------------------------------------------===//

#include "ComputeGraph.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/MapVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMVERIFYCOMPUTEGRAPHPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimVerifyComputeGraphPass
    : public impl::ObeliskSimVerifyComputeGraphPassBase<
          ObeliskSimVerifyComputeGraphPass> {
public:
  void runOnOperation() override;
};

/// Compare one persisted attribute against what the analysis re-derived.
/// `expected` is null when the operation must not carry the attribute at all.
template <typename AttrT>
LogicalResult compareSite(Operation *operation, StringRef what, AttrT actual,
                          const llvm::MapVector<Operation *, AttrT> &expected) {
  auto found = expected.find(operation);
  AttrT wanted = found == expected.end() ? AttrT{} : found->second;
  if (actual == wanted)
    return success();
  if (!wanted)
    return operation->emitOpError("has a ") << what << " it must not carry";
  if (!actual)
    return operation->emitOpError("is missing its ") << what;
  return operation->emitOpError("has a stale ")
         << what << "; the executable CFG derives " << wanted;
}

void ObeliskSimVerifyComputeGraphPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  auto graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError("has no typed compute_graph metadata");
    return signalPassFailure();
  }
  if (failed(simlowering::validateComputeGraphStructure(design, graph)))
    return signalPassFailure();

  // The graph records the options it was derived under, so re-deriving needs
  // nothing beyond the IR itself.
  simlowering::ComputeGraphOptions options;
  options.workers = graph.getWorkers();
  options.vpi = graph.getVpi();
  FailureOr<simlowering::ComputeGraphResult> derived =
      simlowering::deriveComputeGraph(design, options);
  if (failed(derived))
    return signalPassFailure();

  if (derived->graph != graph) {
    // Whole-graph inequality is useless on its own; point at the first element
    // that actually differs.
    InFlightDiagnostic diagnostic =
        design.emitOpError("compute graph does not match the executable CFG");
    auto report = [&](StringRef what, ArrayAttr actual, ArrayAttr expected) {
      if (actual == expected)
        return;
      if (actual.size() != expected.size()) {
        diagnostic.attachNote()
            << "has " << actual.size() << " " << what << "s, but the CFG "
            << "derives " << expected.size();
        return;
      }
      for (auto [index, pair] : llvm::enumerate(llvm::zip(actual, expected)))
        if (std::get<0>(pair) != std::get<1>(pair)) {
          diagnostic.attachNote()
              << what << " " << index << " is " << std::get<0>(pair)
              << ", but the CFG " << "derives " << std::get<1>(pair);
          return;
        }
    };
    if (graph.getVpi() != derived->graph.getVpi() ||
        graph.getWorkers() != derived->graph.getWorkers())
      diagnostic.attachNote()
          << "graph options differ from the ones re-derived";
    report("node", graph.getNodes(), derived->graph.getNodes());
    report("edge", graph.getEdges(), derived->graph.getEdges());
    report("region", graph.getRegions(), derived->graph.getRegions());
    return signalPassFailure();
  }

  bool invalid = false;
  auto check = [&](LogicalResult result) { invalid |= failed(result); };
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    Operation *key = function.getOperation();
    if (function.getEffectSummaryAttr() !=
        derived->effectSummaries.lookup(key)) {
      function.emitOpError("effect summary does not match the executable CFG");
      invalid = true;
    }
    if (function.getFragmentAbiAttr() != derived->fragmentAbis.lookup(key)) {
      function.emitOpError("fragment ABI does not match its CFG blocks");
      invalid = true;
    }
  }

  design.walk([&](Operation *operation) {
    if (simlowering::isSuspensionTerminator(operation))
      check(compareSite(operation, "continuation site",
                        simlowering::getContinuationSite(operation),
                        derived->continuations));
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
      check(compareSite(operation, "timing site", delay.getTimingAttr(),
                        derived->timings));
    if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation))
      check(compareSite(operation, "NBA site", nba.getSiteAttr(),
                        derived->nbaSites));
    if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(operation))
      check(compareSite(operation, "deferred-event site", trigger.getSiteAttr(),
                        derived->eventSites));
  });

  auto checkObservability = [&](auto descriptor) {
    std::optional<sim::ComputeObservabilityKind> actual =
        descriptor.getObservability();
    if (!actual || *actual != derived->observability) {
      descriptor.emitOpError(
          "observability does not match compute-graph VPI mode");
      invalid = true;
    }
  };
  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    checkObservability(storage);
  for (sim::SimNetDeclOp net :
       design.getBody().front().getOps<sim::SimNetDeclOp>())
    checkObservability(net);

  if (invalid)
    signalPassFailure();
}

} // namespace
} // namespace obelisk
