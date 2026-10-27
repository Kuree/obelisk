#ifndef TALOS_DIALECT_SCHEDULE_SCHEDULEOPS_H
#define TALOS_DIALECT_SCHEDULE_SCHEDULEOPS_H
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "talos/Dialect/Runtime/RuntimeTypes.h"
#include "talos/Dialect/Schedule/ScheduleAttrs.h"
#include "talos/Dialect/Simulation/SimulationAttrs.h"
#include "talos/Dialect/Simulation/SimulationResources.h"
#include "talos/Dialect/Simulation/SimulationTypes.h"
#define GET_OP_CLASSES
#include "talos/Dialect/Schedule/ScheduleOps.h.inc"
namespace talos::schedule {
uint32_t getNativeWaitEntryCount(mlir::Operation *operation);
}
#endif
