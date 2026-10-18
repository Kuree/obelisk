//===- AnnotateSteadyUnknown.cpp - Record storage bits that stay X/Z -----===//

#include "obelisk/Analysis/SteadyUnknownAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/IR/BuiltinOps.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMANNOTATESTEADYUNKNOWNPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
class AnnotateSteadyUnknownPass final
    : public impl::ObeliskSimAnnotateSteadyUnknownPassBase<
          AnnotateSteadyUnknownPass> {
public:
  using Base::Base;

  void runOnOperation() override {
    sim::SimDesignOp design = getOperation();
    if (design.getBody().empty())
      return markAllAnalysesPreserved();
    for (auto storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>())
      sim::remove<sim::metadata::SteadyUnknown>(storage);
    // IEEE 1800-2023 38.34: a writable VPI client can deposit any value.
    if (vpi == "full")
      return;
    auto analysis = analysis::SteadyUnknownAnalysis::compute(design);
    if (failed(analysis))
      return signalPassFailure();
    if (analysis->isTop())
      return;
    for (auto storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      auto ranges = analysis->lookup(storage.getId());
      if (ranges.empty())
        continue;
      SmallVector<int64_t> pairs;
      for (auto [low, width] : ranges) {
        pairs.push_back(static_cast<int64_t>(low));
        pairs.push_back(static_cast<int64_t>(width));
        numBits += width;
      }
      ++numRoots;
      sim::set<sim::metadata::SteadyUnknown>(
          storage, DenseI64ArrayAttr::get(&getContext(), pairs));
    }
  }
};
} // namespace
} // namespace obelisk
