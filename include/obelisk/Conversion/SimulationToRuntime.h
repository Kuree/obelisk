//===- SimulationToRuntime.h - Simulation I/O to runtime ------*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTORUNTIME_H
#define OBELISK_CONVERSION_SIMULATIONTORUNTIME_H

#include "obelisk/Conversion/Passes.h"

#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
class RewritePatternSet;
} // namespace mlir

namespace obelisk {

/// Add the bridge-owned type conversions to a native converter. This only
/// maps immutable simulation literal bytes; context and packed-value mappings
/// remain owned by the composing native pipeline.
void addSimulationToRuntimeTypeConversions(mlir::TypeConverter &converter);

/// Populate patterns for precise display and packed file operations. Patterns
/// use 1:N adaptors so a composing converter may represent four-state values
/// as separate value and unknown planes.
void populateSimulationToRuntimePatterns(
    const mlir::TypeConverter &converter, mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTORUNTIME_H
