//===- InsertGCPolls.cpp - Managed-heap collection polls -----------------===//

#include "talos/Analysis/ManagedCollectionAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/IR/Builders.h"

using namespace mlir;

namespace talos {

#define GEN_PASS_DEF_TALOSSIMINSERTGCPOLLSPASS
#include "talos/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

namespace sim = ::talos::sim;

class TalosSimInsertGCPollsPass final
    : public impl::TalosSimInsertGCPollsPassBase<
          TalosSimInsertGCPollsPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto &collections = getAnalysis<analysis::ManagedCollectionAnalysis>();
    OpBuilder builder(module.getContext());
    module.walk([&](sim::SimFuncOp function) {
      if (function.isExternal())
        return;
      Value context = function.getBody().front().getArgument(0);
      for (Operation *site : collections.getPollSites(function)) {
        // A rerun finds its earlier poll at, or just before, the site.
        if (isa<sim::SimGCSafepointOp>(site) ||
            isa_and_nonnull<sim::SimGCSafepointOp>(site->getPrevNode()))
          continue;
        builder.setInsertionPoint(site);
        sim::SimGCSafepointOp::create(builder, site->getLoc(), context);
        ++numPolls;
      }
      function.walk([&](Operation *operation) {
        if (!analysis::isCompiledCall(operation))
          return;
        if (collections.getCallSummary(operation) &
            analysis::ManagedCollectionAnalysis::Polls) {
          sim::remove<sim::metadata::GcPollFree>(operation);
          return;
        }
        sim::mark<sim::metadata::GcPollFree>(operation);
        ++numPollFreeCalls;
      });
    });
    sim::mark<sim::metadata::GCPolled>(module);
  }
};

} // namespace
} // namespace talos
