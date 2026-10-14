//===- InsertGCPolls.cpp - Managed-heap collection polls -----------------===//

#include "obelisk/Analysis/ManagedCollectionAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/IR/Builders.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMINSERTGCPOLLSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

namespace sim = ::obelisk::sim;

class ObeliskSimInsertGCPollsPass final
    : public impl::ObeliskSimInsertGCPollsPassBase<
          ObeliskSimInsertGCPollsPass> {
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
          operation->removeAttr(analysis::gcPollFreeAttrName);
          return;
        }
        operation->setAttr(analysis::gcPollFreeAttrName,
                           builder.getUnitAttr());
        ++numPollFreeCalls;
      });
    });
    sim::mark<sim::GCPolled>(module);
  }
};

} // namespace
} // namespace obelisk
