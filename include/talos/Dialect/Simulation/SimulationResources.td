#ifndef TALOS_SIMULATION_RESOURCES_TD
#define TALOS_SIMULATION_RESOURCES_TD
include "mlir/Interfaces/SideEffectInterfaces.td"
def SimStorageResource : Resource<"::talos::sim::StorageResource">;
def SimNetResource : Resource<"::talos::sim::NetResource">;
def SimSchedulerResource : Resource<"::talos::sim::SchedulerResource">;
def SimProcessResource : Resource<"::talos::sim::ProcessResource">;
def SimHeapResource : Resource<"::talos::sim::HeapResource">;
def SimIOResource : Resource<"::talos::sim::IOResource">;
def SimRNGResource : Resource<"::talos::sim::RNGResource">;
def SimStochasticQueueResource
    : Resource<"::talos::sim::StochasticQueueResource">;
def SimCoverageResource : Resource<"::talos::sim::CoverageResource">;
def SimExternalResource : Resource<"::talos::sim::ExternalResource">;
// Descriptor declarations populate the design inventory. Modelling that as a
// write keeps them inside the effect system and, because they have no results,
// keeps dead-code elimination from deleting the design out from under the
// code units that reference its descriptors by ID.
def SimInventoryResource : Resource<"::talos::sim::InventoryResource">;

#endif
