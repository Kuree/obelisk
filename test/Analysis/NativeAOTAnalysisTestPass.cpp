//===- NativeAOTAnalysisTestPass.cpp - Print native AOT facts -----------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class NativeAOTAnalysisTestPass
    : public PassWrapper<NativeAOTAnalysisTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NativeAOTAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-native-aot-analysis";
  }
  StringRef getDescription() const final {
    return "print native AOT scheduler eligibility facts";
  }

  void runOnOperation() final {
    obelisk::analysis::NativeAOTAnalysis analysis =
        obelisk::analysis::NativeAOTAnalysis::compute(getOperation());
    llvm::errs() << "native-aot eligible="
                 << (analysis.isEligible() ? "true" : "false")
                 << " fully=" << (analysis.isFullyEligible() ? "true" : "false")
                 << "\n";

    SmallVector<std::pair<StringRef, uint32_t>> actors;
    for (auto [operation, slot] : analysis.getActorSlots())
      if (auto function = dyn_cast<obelisk::sim::SimFuncOp>(operation))
        actors.emplace_back(function.getSymName(), slot);
    llvm::sort(actors, [](const auto &left, const auto &right) {
      if (left.second != right.second)
        return left.second < right.second;
      return left.first < right.first;
    });
    for (auto [name, slot] : actors)
      llvm::errs() << "  actor " << slot << " @" << name << "\n";

    struct BytecodeFragment {
      StringRef function;
      unsigned block;
    };
    SmallVector<BytecodeFragment> bytecodeFragments;
    for (const auto &[operation, blocks] : analysis.getBytecodeFragments()) {
      auto function = dyn_cast<obelisk::sim::SimFuncOp>(operation);
      if (!function)
        continue;
      SmallVector<Block *> orderedBlocks;
      function.walk<WalkOrder::PreOrder>(
          [&](Block *block) { orderedBlocks.push_back(block); });
      for (Block *block : blocks) {
        auto found = llvm::find(orderedBlocks, block);
        if (found != orderedBlocks.end())
          bytecodeFragments.push_back({
              function.getSymName(),
              static_cast<unsigned>(found - orderedBlocks.begin()),
          });
      }
    }
    llvm::sort(bytecodeFragments, [](const auto &left, const auto &right) {
      if (left.function != right.function)
        return left.function < right.function;
      return left.block < right.block;
    });
    for (const BytecodeFragment &fragment : bytecodeFragments)
      llvm::errs() << "  bytecode @" << fragment.function << " bb"
                   << fragment.block << "\n";

    for (StringRef reason : analysis.getReasons())
      llvm::errs() << "  reason " << reason << "\n";
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace obelisk {

void registerNativeAOTAnalysisTestPass() {
  PassRegistration<NativeAOTAnalysisTestPass>();
}

} // namespace obelisk
