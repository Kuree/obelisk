//===- SimulationTypes.h - Executable simulation types ---------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H

#include "obelisk/Dialect/Simulation/SimulationDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Simulation/SimulationEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.h.inc"

namespace obelisk::sim {

/// Bit width of a normalized packed simulation value, which is either a
/// signless builtin integer or an exact four-state `!obelisk_sim.logic`.
/// Returns `std::nullopt` for every other type.
std::optional<unsigned> getPackedWidth(::mlir::Type type);

/// Scalar representation used by packed-value operators. Packed aggregates
/// map to an integer or logic value of the same width and state domain;
/// unpacked values have no scalar representation.
::mlir::Type getPackedScalarType(::mlir::Type type);

/// Whether `type` is one of the fixed first-class aggregate types.
bool isAggregateType(::mlir::Type type);

/// Number and element type of declaration-order aggregate subelements.
unsigned getAggregateNumElements(::mlir::Type type);
::mlir::Type getAggregateElementType(::mlir::Type type, unsigned index);

/// Convert a source array index into its zero-based declaration ordinal.
std::optional<unsigned> getArrayElementOrdinal(::mlir::Type type,
                                               int64_t sourceIndex);

/// Structural span used only by descriptor provenance analysis. Unlike packed
/// width, this assigns disjoint intervals to unpacked struct/array children.
std::optional<uint64_t> getProvenanceSpan(::mlir::Type type);

/// Structural offset/span for one declaration-order child. Union children all
/// overlap at offset zero.
std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(::mlir::Type type, unsigned index);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
