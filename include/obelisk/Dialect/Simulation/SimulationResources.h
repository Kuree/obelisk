#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#include "mlir/Interfaces/SideEffectInterfaces.h"
namespace obelisk::sim {

#define OBELISK_SIM_RESOURCE(Name, Text)                                       \
  struct Name : public ::mlir::SideEffects::Resource::Base<Name> {             \
    ::llvm::StringRef getName() final { return Text; }                         \
  }

OBELISK_SIM_RESOURCE(StorageResource, "obelisk_sim.storage");
OBELISK_SIM_RESOURCE(NetResource, "obelisk_sim.net");
OBELISK_SIM_RESOURCE(SchedulerResource, "obelisk_sim.scheduler");
OBELISK_SIM_RESOURCE(ProcessResource, "obelisk_sim.process");
OBELISK_SIM_RESOURCE(HeapResource, "obelisk_sim.heap");
OBELISK_SIM_RESOURCE(IOResource, "obelisk_sim.io");
OBELISK_SIM_RESOURCE(RNGResource, "obelisk_sim.rng");
OBELISK_SIM_RESOURCE(StochasticQueueResource, "obelisk_sim.stochastic_queue");
OBELISK_SIM_RESOURCE(CoverageResource, "obelisk_sim.coverage");
OBELISK_SIM_RESOURCE(ExternalResource, "obelisk_sim.external");
OBELISK_SIM_RESOURCE(InventoryResource, "obelisk_sim.inventory");

#undef OBELISK_SIM_RESOURCE

} // namespace obelisk::sim
#endif
