#include "mlir/Pass/PassManager.h"
#include "obelisk/Conversion/Passes.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
namespace obelisk {
void buildSimulationToLLVMCoroutinePipeline(mlir::OpPassManager &manager,
                                            bool nativeWords) {
  manager.addPass(createPrepareNativeScheduleInputsPass());
  manager.addPass(createPlanNativeStatePass());
  manager.addPass(createPrepareNativeProcessFramesPass());
  manager.addPass(createPlanNativeTransfersPass());
  manager.addPass(createShareNativeTransfersPass());
  manager.addPass(createPlanNativeActorsPass());
  manager.addPass(createSpecializeNativeCapturesPass());
  manager.addPass(createPlanNativeSchedulePass());
  manager.addPass(createPrepareNativeManagedRootsPass());
  manager.addPass(createMarkCleanNativeNBAPass());
  manager.addPass(createSpecializeNativeEvalPass());
  if (nativeWords)
    manager.addPass(createOptimizeNativeWordsPass());
  manager.addPass(createLowerNativeWordRegionsPass());
  manager.addPass(createPrepareNativeTransferKernelsPass());
  manager.addPass(createAnnotateCompactNativeNBAPass());
  manager.addPass(createPrepareNativeFragmentsPass());
  manager.addPass(createPlanNativeEvalOwnershipPass());
  manager.addPass(createAnnotateCompactNativeNBAPass());
  manager.addPass(createPlanNativeExecutableNodesPass());
  manager.addPass(createResolveNativeEvalSchedulePass());
  manager.addPass(createMaterializeNativeProcessesPass());
  manager.addPass(createConvertPreparedSimProcessesToLLVMCoroutinesPass());
}
void registerSimulationToLLVMCoroutinePipeline() {
  struct Options : mlir::PassPipelineOptions<Options> {
    Option<bool> nativeWords{
        *this, "native-words",
        llvm::cl::desc("Optimize independent native word lanes"),
        llvm::cl::init(true)};
  };
  mlir::PassPipelineRegistration<Options>(
      "convert-obelisk-sim-processes-to-llvm-coroutines",
      "Plan, specialize, and lower native simulation processes",
      [](mlir::OpPassManager &manager, const Options &options) {
        buildSimulationToLLVMCoroutinePipeline(manager, options.nativeWords);
      });
}
} // namespace obelisk
