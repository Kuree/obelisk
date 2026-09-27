#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#include "mlir/Interfaces/SideEffectInterfaces.h"
namespace obelisk::sim {

#define OBELISK_SIM_RESOURCE(Name, Text)                                       \
  struct Name : public ::mlir::SideEffects::Resource::Base<Name> {             \
    ::llvm::StringRef getName() final { return Text; }                         \
  }

OBELISK_SIM_RESOURCE(StorageResource, "simulation.storage");
OBELISK_SIM_RESOURCE(NetResource, "simulation.net");
OBELISK_SIM_RESOURCE(SchedulerResource, "simulation.scheduler");
OBELISK_SIM_RESOURCE(ProcessResource, "simulation.process");
OBELISK_SIM_RESOURCE(HeapResource, "simulation.heap");
OBELISK_SIM_RESOURCE(IOResource, "simulation.io");
OBELISK_SIM_RESOURCE(RNGResource, "simulation.rng");
OBELISK_SIM_RESOURCE(StochasticQueueResource, "simulation.stochastic_queue");
OBELISK_SIM_RESOURCE(CoverageResource, "simulation.coverage");
OBELISK_SIM_RESOURCE(ExternalResource, "simulation.external");
OBELISK_SIM_RESOURCE(InventoryResource, "simulation.inventory");

#undef OBELISK_SIM_RESOURCE

} // namespace obelisk::sim
#endif
