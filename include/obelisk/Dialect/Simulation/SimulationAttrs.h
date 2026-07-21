//===- SimulationAttrs.h - Late simulation analysis attributes -*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_H

#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationAttrs.h.inc"

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_H
