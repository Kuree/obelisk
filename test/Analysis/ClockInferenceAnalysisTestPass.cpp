//===- ClockInferenceAnalysisTestPass.cpp - Print inferred clock domains
//----===//

#include "AnalysisTestPasses.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;

namespace {
class ClockInferenceAnalysisTestPass
    : public PassWrapper<ClockInferenceAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ClockInferenceAnalysisTestPass)
  StringRef getArgument() const final { return "test-obelisk-clock-inference"; }
  StringRef getDescription() const final {
    return "print inferred clock domains";
  }
  void runOnOperation() final {
    using namespace obelisk::analysis;
    auto layout = NativeStateLayoutAnalysis::compute(getOperation());
    if (failed(layout)) {
      signalPassFailure();
      return;
    }
    SmallVector<PeriodicClockSeed> seeds;
    getOperation().walk([&](obelisk::sim::SimFuncOp function) {
      auto storage = function->getAttrOfType<IntegerAttr>("test.clock_storage");
      auto period =
          function->getAttrOfType<IntegerAttr>("test.clock_half_period");
      if (storage && period) {
        uint64_t offset = layout->storageOffsets.lookup(storage.getUInt());
        auto bound = llvm::find_if(layout->bounds, [&](const auto &bound) {
          return bound.offset == offset;
        });
        if (bound != layout->bounds.end())
          seeds.push_back(
              {{bound->handleID, offset}, function, period.getUInt()});
      }
    });
    ClockInferenceAnalysis inference(getOperation(), *layout, seeds);
    auto print = [&](StringRef kind, const llvm::DenseMap<uint64_t, uint64_t>
                                         &offsets) {
      SmallVector<uint64_t> descriptors;
      for (const auto &entry : offsets)
        descriptors.push_back(entry.first);
      llvm::sort(descriptors);
      for (uint64_t descriptor : descriptors)
        printFact(kind, descriptor, offsets.lookup(descriptor), *layout,
                  inference);
    };
    print("clock", layout->storageOffsets);
    print("net", layout->netOffsets);
    markAllAnalysesPreserved();
  }

  static void printFact(StringRef kind, uint64_t descriptor, uint64_t offset,
                        const obelisk::analysis::NativeStateLayoutAnalysis
                            &layout,
                        const obelisk::analysis::ClockInferenceAnalysis
                            &inference) {
    using namespace obelisk::analysis;
    {
      auto bound = llvm::find_if(layout.bounds, [&](const auto &bound) {
        return bound.offset == offset;
      });
      if (bound == layout.bounds.end())
        return;
      ClockBit bit{bound->handleID, offset};
      ClockFact fact = inference.lookup(bit);
      llvm::errs() << kind << " " << descriptor << ": ";
      switch (fact.kind) {
      case ClockFact::Kind::Bottom:
        llvm::errs() << "bottom";
        break;
      case ClockFact::Kind::Unknown:
        llvm::errs() << "unknown";
        break;
      case ClockFact::Kind::Constant:
        llvm::errs() << "constant=" << unsigned(fact.constant);
        break;
      case ClockFact::Kind::Periodic:
        llvm::errs() << "periodic";
        break;
      case ClockFact::Kind::TickDriven:
        llvm::errs() << "tick-driven";
        break;
      }
      if (fact.hasTickBound())
        llvm::errs() << " domain=" << fact.source.first << ":"
                     << fact.source.second
                     << " half-period=" << fact.halfPeriod;
      if (fact.inverted)
        llvm::errs() << " inverted";
      ClockOrigin origin = inference.getOrigin(bit);
      if (origin.bit != bit)
        llvm::errs() << " origin=" << origin.bit.first << ":"
                     << origin.bit.second << (origin.inverted ? " complement" : "");
      llvm::errs() << '\n';
    }
  }
};
} // namespace

void obelisk::registerClockInferenceAnalysisTestPass() {
  PassRegistration<ClockInferenceAnalysisTestPass>();
}
