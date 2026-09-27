#ifndef OBELISK_SIMULATION_RESOURCES_TD
#define OBELISK_SIMULATION_RESOURCES_TD
include "mlir/Interfaces/SideEffectInterfaces.td"
def SimStorageResource : Resource<"::obelisk::sim::StorageResource">;
def SimNetResource : Resource<"::obelisk::sim::NetResource">;
def SimSchedulerResource : Resource<"::obelisk::sim::SchedulerResource">;
def SimProcessResource : Resource<"::obelisk::sim::ProcessResource">;
def SimHeapResource : Resource<"::obelisk::sim::HeapResource">;
def SimIOResource : Resource<"::obelisk::sim::IOResource">;
def SimRNGResource : Resource<"::obelisk::sim::RNGResource">;
def SimStochasticQueueResource
    : Resource<"::obelisk::sim::StochasticQueueResource">;
def SimCoverageResource : Resource<"::obelisk::sim::CoverageResource">;
def SimExternalResource : Resource<"::obelisk::sim::ExternalResource">;
// Descriptor declarations populate the design inventory. Modelling that as a
// write keeps them inside the effect system and, because they have no results,
// keeps dead-code elimination from deleting the design out from under the
// code units that reference its descriptors by ID.
def SimInventoryResource : Resource<"::obelisk::sim::InventoryResource">;

#endif
