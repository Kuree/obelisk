//===- EliminateDeadCaptures.cpp - Prune simulation entry captures -------===//

#include "EliminateDeadBoundaries.h"

#include "talos/Dialect/Simulation/Transforms/Passes.h"

using namespace mlir;

namespace talos {

#define GEN_PASS_DEF_TALOSSIMELIMINATEDEADCAPTURESPASS
#include "talos/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

class TalosSimEliminateDeadCapturesPass final
    : public impl::TalosSimEliminateDeadCapturesPassBase<
          TalosSimEliminateDeadCapturesPass> {
public:
  using Base = impl::TalosSimEliminateDeadCapturesPassBase<
      TalosSimEliminateDeadCapturesPass>;
  using Base::Base;
  TalosSimEliminateDeadCapturesPass(
      const TalosSimEliminateDeadCapturesPass &other)
      : Base(other) {}

  void runOnOperation() override {
    EliminationStatistics statistics{&functionsConsidered,
                                     &abiPinnedFunctions,
                                     &functionsPruned,
                                     &argumentsRemoved,
                                     nullptr,
                                     &callOperandsRemoved,
                                     &spawnOperandsRemoved,
                                     &taskOperandsRemoved};
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
  Statistic taskOperandsRemoved{this, "task-operands-removed",
                                "direct task-call operands removed"};
};

} // namespace
} // namespace talos
