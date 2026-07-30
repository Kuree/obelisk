//===- SimulationTimeLowering.h - Normalize simulation time ----*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTIMELOWERING_H
#define OBELISK_CONVERSION_SIMULATIONTIMELOWERING_H

#include "mlir/IR/Operation.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
class RewritePatternSet;
}

namespace obelisk {

/// Populate pure rewrites from simulation time and real conversion operations
/// to arith. Time-valued results require their backend consumer boundary to be
/// normalized in the same rewrite transaction. These patterns do not
/// introduce runtime calls.
void populateSimulationTimeLoweringPatterns(mlir::RewritePatternSet &patterns);

/// Apply the complete time normalization beneath `root`.
mlir::LogicalResult lowerSimulationTimeOperations(mlir::Operation *root);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTIMELOWERING_H
