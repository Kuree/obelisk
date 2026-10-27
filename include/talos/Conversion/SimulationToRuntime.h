//===- SimulationToRuntime.h - Simulation I/O to runtime ------*- C++ -*-===//

#ifndef TALOS_CONVERSION_SIMULATIONTORUNTIME_H
#define TALOS_CONVERSION_SIMULATIONTORUNTIME_H

#include "talos/Conversion/Passes.h"

#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
class RewritePatternSet;
} // namespace mlir

namespace talos {

/// Add the bridge-owned type conversions to a native converter. This only
/// maps immutable simulation literal bytes; context and packed-value mappings
/// remain owned by the composing native pipeline.
void addSimulationToRuntimeTypeConversions(mlir::TypeConverter &converter);

/// Populate patterns for precise display and packed file operations. Patterns
/// use 1:N adaptors so a composing converter may represent four-state values
/// as separate value and unknown planes.
void populateSimulationToRuntimePatterns(const mlir::TypeConverter &converter,
                                         mlir::RewritePatternSet &patterns);

} // namespace talos

#endif // TALOS_CONVERSION_SIMULATIONTORUNTIME_H
