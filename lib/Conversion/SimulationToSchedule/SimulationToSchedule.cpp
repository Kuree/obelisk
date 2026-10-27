#include "talos/Conversion/SimulationToSchedule.h"
#include "mlir/Pass/PassManager.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
namespace talos {
void registerSimulationToSchedulePipeline() {
  mlir::PassPipelineRegistration<>(
      "convert-simulation-to-schedule",
      "Derive the verified schedule from executable simulation IR",
      [](mlir::OpPassManager &manager) {
        manager.addNestedPass<sim::SimDesignOp>(
            createTalosSimBuildComputeGraphPass());
      });
}
} // namespace talos
