#include "obelisk/Conversion/SimulationToSchedule.h"
#include "mlir/Pass/PassManager.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
namespace obelisk {
void registerSimulationToSchedulePipeline() {
  mlir::PassPipelineRegistration<>(
      "convert-simulation-to-schedule",
      "Derive the verified schedule from executable simulation IR",
      [](mlir::OpPassManager &manager) {
        manager.addNestedPass<sim::SimDesignOp>(
            createObeliskSimBuildComputeGraphPass());
      });
}
} // namespace obelisk
