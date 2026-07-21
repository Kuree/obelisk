//===- ObeliskToSimulation.h - Semantic to executable simulation -*- C++
//-*-===//

#ifndef OBELISK_CONVERSION_OBELISKTOSIMULATION_H
#define OBELISK_CONVERSION_OBELISKTOSIMULATION_H

#include "obelisk/Conversion/Passes.h"

namespace mlir {
class OpPassManager;
}

namespace obelisk {

/// Populate a module pass manager with the serial/parallel/serial lowering.
void buildObeliskToSimulationPipeline(mlir::OpPassManager &manager);

/// Register the aggregate serial/parallel/serial lowering pipeline.
void registerObeliskToSimulationPipeline();

} // namespace obelisk

#endif // OBELISK_CONVERSION_OBELISKTOSIMULATION_H
