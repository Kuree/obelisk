//===- Passes.h - Simulation transformation passes -----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
#define OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"

namespace obelisk {

#define GEN_PASS_DECL
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

/// Build the graph pass for the pipeline's post-fusion stage. The existing
/// graph is known current when body fusion retained it; this internal factory
/// avoids exposing unsafe graph reuse as a textual pass option.
std::unique_ptr<mlir::Pass> createObeliskSimBuildCurrentComputeGraphPass(
    ObeliskSimBuildComputeGraphPassOptions options);

#define GEN_PASS_REGISTRATION
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

} // namespace obelisk

#endif // OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_H
