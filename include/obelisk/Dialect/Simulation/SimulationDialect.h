//===- SimulationDialect.h - Executable simulation dialect -----*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H

#include "obelisk/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

#include "obelisk/Dialect/Simulation/SimulationDialect.h.inc"

namespace obelisk::sim {

using IntegerRange = ObeliskSimulationDialect::IntegerRangeAttrHelper;
using ResultRanges = ObeliskSimulationDialect::ResultRangesAttrHelper;
using ResultExprs = ObeliskSimulationDialect::ResultExprsAttrHelper;
using KnownLogicResult = ObeliskSimulationDialect::KnownLogicResultAttrHelper;
using GCPolled = ObeliskSimulationDialect::GcPolledAttrHelper;
using InlineLegalityCache = ObeliskSimulationDialect::InlineLegalityAttrHelper;
using PulseRejectPercent =
    ObeliskSimulationDialect::PulseRejectPercentAttrHelper;
using PulseErrorPercent = ObeliskSimulationDialect::PulseErrorPercentAttrHelper;
using PulseOnDetect = ObeliskSimulationDialect::PulseOnDetectAttrHelper;
using PulseShowCancelled =
    ObeliskSimulationDialect::PulseShowCancelledAttrHelper;

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H
