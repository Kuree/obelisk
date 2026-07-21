//===- SimulationTypes.h - Executable simulation types ---------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H

#include "obelisk/Dialect/Simulation/SimulationDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Simulation/SimulationEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.h.inc"

namespace obelisk::sim {

/// Bit width of a normalized packed simulation value, which is either a
/// signless builtin integer or an exact four-state `!obelisk_sim.logic`.
/// Returns `std::nullopt` for every other type.
std::optional<unsigned> getPackedWidth(::mlir::Type type);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
