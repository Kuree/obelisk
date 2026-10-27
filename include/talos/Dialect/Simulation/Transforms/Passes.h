//===- Passes.h - Simulation transformation passes -----------*- C++ -*-===//

#ifndef TALOS_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
#define TALOS_DIALECT_SIMULATION_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"

namespace talos {

#define GEN_PASS_DECL
#include "talos/Dialect/Simulation/Transforms/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "talos/Dialect/Simulation/Transforms/Passes.h.inc"

} // namespace talos

#endif // TALOS_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
