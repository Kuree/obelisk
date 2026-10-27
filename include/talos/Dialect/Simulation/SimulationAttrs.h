//===- SimulationAttrs.h - Late simulation analysis attributes -*- C++ -*-===//

#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONATTRS_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONATTRS_H

#include "talos/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"

#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/Simulation/SimulationAttrs.h.inc"

#endif // TALOS_DIALECT_SIMULATION_SIMULATIONATTRS_H
