#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"

using namespace mlir;
namespace obelisk::detail {
LogicalResult NativePipelineAnalysis::specializeCaptures() {
  if (bytecodeOnly)
    return success();
  if (aotEligibility.isEligible() &&
      failed(specializeNativeAOTCaptures(module, aotEligibility)))
    return failure();

  return success();
}

LogicalResult NativePipelineAnalysis::markCleanNBA() {
  if (bytecodeOnly)
    return success();
  if (failed(markCleanStaticNBAsInGuardedBodies(
          module, guardedAOTSpecialization, staticNBAPlan.siteRoots,
          staticNBAPlan.roots, *stateLayout)))
    return failure();

  return success();
}

LogicalResult NativePipelineAnalysis::specializeEval() {
  if (bytecodeOnly)
    return success();
  // Continuous stores retain canonical publication until clean lowering
  // records the corresponding state planes.
  bool hasContinuousStore = false;
  module.walk([&](sim::SimRefStoreOp store) {
    auto kind = store->getParentOfType<sim::SimFuncOp>().getEntryKind();
    hasContinuousStore |= store->hasAttr("obelisk_sim.continuous_store") ||
                          kind == sim::EntryKind::Continuous ||
                          kind == sim::EntryKind::PortInput ||
                          kind == sim::EntryKind::PortOutput;
  });
  cleanWritableEval = evalScheduler && vpi.allowsWrite() &&
                      !hasLanguageOverride && !hasContinuousStore;
  stateLayout->directContinuous =
      directStaticState && !useAOT && vpi.allowsWrite() && hasContinuousStore;
  if (cleanWritableEval)
    materializeCleanEvalBodies(metadataDesign);
  auto evalStateLayout = cleanWritableEval
                             ? detail::makeCleanEvalStateLayout(*stateLayout)
                             : *stateLayout;
  if (failed(materializeEvalTwoStateVariants(module, metadataDesign,
                                             evalStateLayout, evalScheduler,
                                             aotActorSlotsByCodeUnit)))
    return failure();
  markTiming("schedule ranks, roots, and two-state variants");

  return success();
}

} // namespace obelisk::detail

namespace obelisk {
#define GEN_PASS_DEF_SPECIALIZENATIVECAPTURESPASS
#define GEN_PASS_DEF_MARKCLEANNATIVENBAPASS
#define GEN_PASS_DEF_SPECIALIZENATIVEEVALPASS
#define GEN_PASS_DEF_ANNOTATECOMPACTNATIVENBAPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"
namespace {
class SpecializeNativeCapturesPass final
    : public impl::SpecializeNativeCapturesPassBase<
          SpecializeNativeCapturesPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-specialize-native-captures requires "
                               "the native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Actors) {
      getOperation().emitError("schedule-specialize-native-captures requires "
                               "native pipeline phase Actors");
      return signalPassFailure();
    }
    if (failed(state.specializeCaptures()))
      return signalPassFailure();
    state.stage = Analysis::Stage::Captures;
    markAnalysesPreserved<Analysis>();
  }
};
class MarkCleanNativeNBAPass final
    : public impl::MarkCleanNativeNBAPassBase<MarkCleanNativeNBAPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-mark-clean-native-nba requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::Roots) {
      getOperation().emitError("schedule-mark-clean-native-nba requires native "
                               "pipeline phase Roots");
      return signalPassFailure();
    }
    if (failed(state.markCleanNBA()))
      return signalPassFailure();
    state.stage = Analysis::Stage::CleanNBA;
    markAnalysesPreserved<Analysis>();
  }
};
class SpecializeNativeEvalPass final
    : public impl::SpecializeNativeEvalPassBase<SpecializeNativeEvalPass> {
  void runOnOperation() override {
    using Analysis = detail::NativePipelineAnalysis;
    auto cached = getCachedAnalysis<Analysis>();
    if (!cached) {
      getOperation().emitError("schedule-specialize-native-eval requires the "
                               "native preparation pipeline analysis");
      return signalPassFailure();
    }
    auto &state = cached->get();
    if (state.stage != Analysis::Stage::CleanNBA) {
      getOperation().emitError("schedule-specialize-native-eval requires "
                               "native pipeline phase CleanNBA");
      return signalPassFailure();
    }
    if (failed(state.specializeEval()))
      return signalPassFailure();
    state.stage = Analysis::Stage::EvalVariants;
    markAnalysesPreserved<Analysis>();
  }
};
class AnnotateCompactNativeNBAPass final
    : public impl::AnnotateCompactNativeNBAPassBase<
          AnnotateCompactNativeNBAPass> {
  void runOnOperation() override {
    detail::annotateCompactNBAMetadata(getOperation());
    // Only operation-local certificates change; frame and owner identities do
    // not.
    markAnalysesPreserved<detail::NativePipelineAnalysis>();
  }
};
} // namespace
} // namespace obelisk
