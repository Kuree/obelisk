//===- Passes.h - Simulation transformation passes -----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
#define OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"

namespace obelisk {

#define GEN_PASS_DECL
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

} // namespace obelisk

#endif // OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
