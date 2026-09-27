#ifndef OBELISK_DIALECT_SCHEDULE_SCHEDULEOPS_H
#define OBELISK_DIALECT_SCHEDULE_SCHEDULEOPS_H
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationResources.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"
#define GET_OP_CLASSES
#include "obelisk/Dialect/Schedule/ScheduleOps.h.inc"
namespace obelisk::schedule {
uint32_t getNativeWaitEntryCount(mlir::Operation *operation);
}
#endif
