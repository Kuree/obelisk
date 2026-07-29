//===- SimulationRuntime.h - Shared simulation/runtime mappings -*- C++ -*-===//
//
// Target-independent mappings used by native and bytecode lowering.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_CONVERSION_SIMULATIONRUNTIME_H
#define OBELISK_CONVERSION_SIMULATIONRUNTIME_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include <cstdint>
#include <limits>

namespace obelisk {

inline uint32_t getRuntimeEventRegion(sim::EventRegion region) {
  switch (region) {
  case sim::EventRegion::Active:
    return OBELISK_RT_REGION_ACTIVE;
  case sim::EventRegion::Observed:
    return OBELISK_RT_REGION_OBSERVED;
  case sim::EventRegion::Reactive:
    return OBELISK_RT_REGION_REACTIVE;
  case sim::EventRegion::Postponed:
    return OBELISK_RT_REGION_POSTPONED;
  default:
    return std::numeric_limits<uint32_t>::max();
  }
}

inline uint32_t getRuntimeResumeActionFlags(mlir::Operation *operation) {
  auto region = operation->getAttrOfType<sim::EventRegionAttr>("resume_region");
  if (!region)
    return 0;
  uint32_t ordinal = getRuntimeEventRegion(region.getValue());
  return ordinal == std::numeric_limits<uint32_t>::max()
             ? ordinal
             : OBELISK_RT_ACTION_RESUME_REGION(ordinal);
}

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONRUNTIME_H
