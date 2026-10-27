#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONRESOURCES_H
#include "mlir/Interfaces/SideEffectInterfaces.h"
namespace talos::sim {

#define TALOS_SIM_RESOURCE(Name, Text)                                       \
  struct Name : public ::mlir::SideEffects::Resource::Base<Name> {             \
    ::llvm::StringRef getName() final { return Text; }                         \
  }

TALOS_SIM_RESOURCE(StorageResource, "simulation.storage");
TALOS_SIM_RESOURCE(NetResource, "simulation.net");
TALOS_SIM_RESOURCE(SchedulerResource, "simulation.scheduler");
TALOS_SIM_RESOURCE(ProcessResource, "simulation.process");
TALOS_SIM_RESOURCE(HeapResource, "simulation.heap");
TALOS_SIM_RESOURCE(IOResource, "simulation.io");
TALOS_SIM_RESOURCE(RNGResource, "simulation.rng");
TALOS_SIM_RESOURCE(StochasticQueueResource, "simulation.stochastic_queue");
TALOS_SIM_RESOURCE(CoverageResource, "simulation.coverage");
TALOS_SIM_RESOURCE(ExternalResource, "simulation.external");
TALOS_SIM_RESOURCE(InventoryResource, "simulation.inventory");

#undef TALOS_SIM_RESOURCE

} // namespace talos::sim
#endif
