//===- NativeStateLayoutAnalysisTestPass.cpp - Print state layout facts -===//

#include "AnalysisTestPasses.h"

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class NativeStateLayoutAnalysisTestPass
    : public PassWrapper<NativeStateLayoutAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      NativeStateLayoutAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-native-state-layout-analysis";
  }
  StringRef getDescription() const final {
    return "print stable native state layout facts";
  }

  void runOnOperation() final {
    FailureOr<obelisk::analysis::NativeStateLayoutAnalysis> layout =
        obelisk::analysis::NativeStateLayoutAnalysis::compute(getOperation());
    if (failed(layout)) {
      signalPassFailure();
      return;
    }

    llvm::errs() << "native-state bits=" << layout->bitCount << "\n";
    for (const auto &bound : layout->bounds) {
      llvm::errs() << "  bound " << bound.handleID << " offset="
                   << bound.offset << " width=" << bound.width
                   << " four-state="
                   << (bound.fourState ? "true" : "false");
      if (!bound.managedRootOffsets.empty()) {
        llvm::errs() << " roots=";
        llvm::interleaveComma(bound.managedRootOffsets, llvm::errs());
      }
      llvm::errs() << "\n";
    }
    for (const auto &net : layout->netLayouts)
      llvm::errs() << "  net " << net.id << " handle=" << net.handleID
                   << " offset=" << net.offset << " width=" << net.width
                   << " four-state=" << (net.fourState ? "true" : "false")
                   << "\n";
    for (const auto &driver : layout->driverLayouts)
      llvm::errs() << "  driver " << driver.id << " net=" << driver.netId
                   << " handle=" << driver.handleID
                   << " offset=" << driver.offset << " width=" << driver.width
                   << " driven=[" << driver.drivenLow << ","
                   << driver.drivenLow + driver.drivenWidth << ")\n";
    markAllAnalysesPreserved();
  }
};

} // namespace

namespace obelisk {

void registerNativeStateLayoutAnalysisTestPass() {
  PassRegistration<NativeStateLayoutAnalysisTestPass>();
}

} // namespace obelisk
