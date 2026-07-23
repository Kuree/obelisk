//===- EliminateDeadCaptures.cpp - Prune simulation entry captures -------===//

#include "EliminateDeadBoundaries.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMELIMINATEDEADCAPTURESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

class ObeliskSimEliminateDeadCapturesPass final
    : public impl::ObeliskSimEliminateDeadCapturesPassBase<
          ObeliskSimEliminateDeadCapturesPass> {
public:
  using Base = impl::ObeliskSimEliminateDeadCapturesPassBase<
      ObeliskSimEliminateDeadCapturesPass>;
  using Base::Base;

  void runOnOperation() override {
    EliminationStatistics statistics{&functionsConsidered,
                                     &abiPinnedFunctions,
                                     &functionsPruned,
                                     &argumentsRemoved,
                                     nullptr,
                                     &callOperandsRemoved,
                                     &spawnOperandsRemoved};
    if (failed(eliminateDeadSimulationBoundaries(getOperation(),
                                                 /*eliminateResults=*/false,
                                                 missedRemarks, statistics)))
      signalPassFailure();
  }

private:
  Statistic functionsConsidered{this, "functions-considered",
                                "simulation functions considered"};
  Statistic abiPinnedFunctions{this, "abi-pinned-functions",
                               "functions whose complete ABI was retained"};
  Statistic functionsPruned{this, "functions-pruned",
                            "functions with arguments removed"};
  Statistic argumentsRemoved{this, "arguments-removed",
                             "function arguments removed"};
  Statistic callOperandsRemoved{this, "call-operands-removed",
                                "direct call operands removed"};
  Statistic spawnOperandsRemoved{this, "spawn-operands-removed",
                                 "direct spawn operands removed"};
};

} // namespace
} // namespace obelisk
