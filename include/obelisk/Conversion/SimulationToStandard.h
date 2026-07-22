//===- SimulationToStandard.h - Lower packed simulation values -*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOSTANDARD_H
#define OBELISK_CONVERSION_SIMULATIONTOSTANDARD_H

#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
class RewritePatternSet;
} // namespace mlir

namespace obelisk {

/// Converts an exact four-state packed value to its value and unknown planes.
/// All non-logic types are deliberately preserved for composition with later
/// runtime lowering. No materializations are registered: every logic boundary
/// must be converted in the same dialect-conversion transaction.
class SimulationToStandardTypeConverter : public mlir::TypeConverter {
public:
  SimulationToStandardTypeConverter();
};

/// Adds conversions for the pure obelisk_sim packed-value operations and the
/// standard func/cf boundaries through which their 1:N values may flow.
/// `converter` must map every `sim::LogicType<W>` to exactly two `iW` values.
/// Function and call argument/result dictionaries are duplicated onto every
/// converted component. Discardable attributes on pure value operations are
/// not propagated because those operations expand into expression graphs with
/// no single equivalent target operation; generated operations retain the
/// source location for provenance.
void populateSimulationToStandardPatterns(const mlir::TypeConverter &converter,
                                          mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTOSTANDARD_H
