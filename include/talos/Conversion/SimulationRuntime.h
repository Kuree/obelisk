//===- SimulationRuntime.h - Shared simulation/runtime mappings -*- C++ -*-===//
//
// Target-independent mappings used by native and bytecode lowering.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_CONVERSION_SIMULATIONRUNTIME_H
#define TALOS_CONVERSION_SIMULATIONRUNTIME_H

#include "talos/Dialect/Simulation/SimulationAttrs.h"
#include "talos/Runtime/Runtime.h"

#include "mlir/IR/Operation.h"

#include <cstdint>
#include <limits>

namespace talos {

inline uint32_t getRuntimeEventRegion(sim::EventRegion region) {
  switch (region) {
  case sim::EventRegion::Active:
    return TALOS_RT_REGION_ACTIVE;
  case sim::EventRegion::Inactive:
    return TALOS_RT_REGION_INACTIVE;
  case sim::EventRegion::ReInactive:
    return TALOS_RT_REGION_RE_INACTIVE;
  case sim::EventRegion::Observed:
    return TALOS_RT_REGION_OBSERVED;
  case sim::EventRegion::Reactive:
    return TALOS_RT_REGION_REACTIVE;
  case sim::EventRegion::Postponed:
    return TALOS_RT_REGION_POSTPONED;
  default:
    return std::numeric_limits<uint32_t>::max();
  }
}

inline uint32_t getRuntimeResumeActionFlags(mlir::Operation *operation) {
  auto region = operation->getAttrOfType<sim::EventRegionAttr>("resume_region");
  if (!region)
    return 0;
  if (region.getValue() == sim::EventRegion::PostObserved)
    return TALOS_RT_ACTION_RESUME_POST_OBSERVED;
  uint32_t ordinal = getRuntimeEventRegion(region.getValue());
  return ordinal == std::numeric_limits<uint32_t>::max()
             ? ordinal
             : TALOS_RT_ACTION_RESUME_REGION(ordinal);
}

} // namespace talos

#endif // TALOS_CONVERSION_SIMULATIONRUNTIME_H
