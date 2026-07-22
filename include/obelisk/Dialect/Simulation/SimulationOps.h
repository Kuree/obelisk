//===- SimulationOps.h - Executable simulation operations -------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H

#include "obelisk/Dialect/Runtime/RuntimeTypes.h"
#include "obelisk/Dialect/Simulation/SimulationAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"

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
OBELISK_SIM_RESOURCE(ExternalResource, "obelisk_sim.external");
OBELISK_SIM_RESOURCE(InventoryResource, "obelisk_sim.inventory");

#undef OBELISK_SIM_RESOURCE

} // namespace obelisk::sim

#define GET_OP_CLASSES
#include "obelisk/Dialect/Simulation/SimulationOps.h.inc"

namespace obelisk::sim {

/// Unconditional semantic legality for inlining a simulation call.  These
/// rules are shared by every MLIR inlining client; profitability and growth
/// policy remain properties of the Obelisk-owned pass.
enum class InlineLegality {
  Legal,
  NotDefinedFunction,
  LateMetadata,
  Recursive,
  UnknownMetadata,
  Suspension,
  UnfrozenDisplayScope,
  UnknownBoundaryMetadata,
};

InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee);
::llvm::StringRef getInlineLegalityReason(InlineLegality legality);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H
