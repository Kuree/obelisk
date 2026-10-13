//===- SimulationTraits.h - Simulation operation traits ----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTRAITS_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTRAITS_H

#include "mlir/IR/OpDefinition.h"

namespace mlir::OpTrait {

/// Marks a simulation operation that may allocate managed storage: a class
/// object, string, container, reference path, or coverage instance. Where code
/// roots managed values eagerly it is a collection point; under polled
/// collection it only records allocation pressure, and a poll must bound it
/// when it repeats. Calls into compiled code are summarized separately.
template <typename ConcreteType>
class AllocatesManagedStorage
    : public TraitBase<ConcreteType, AllocatesManagedStorage> {};

} // namespace mlir::OpTrait

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTRAITS_H
