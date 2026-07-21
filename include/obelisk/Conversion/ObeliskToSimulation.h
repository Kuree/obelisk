//===- ObeliskToSimulation.h - Semantic to executable sim ----*- C++ -*-===//

#ifndef OBELISK_CONVERSION_OBELISKTOSIMULATION_H
#define OBELISK_CONVERSION_OBELISKTOSIMULATION_H

#include "obelisk/Conversion/Passes.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace mlir {
class OpPassManager;
}

namespace obelisk {

/// Populate a module pass manager with the serial/parallel/serial lowering.
void buildObeliskToSimulationPipeline(mlir::OpPassManager &manager);

/// Populate the lowering pipeline with explicit simulation configuration.
void buildObeliskToSimulationPipeline(mlir::OpPassManager &manager,
                                      uint32_t workers,
                                      llvm::StringRef vpiMode);

/// Register the aggregate serial/parallel/serial lowering pipeline.
void registerObeliskToSimulationPipeline();

} // namespace obelisk

#endif // OBELISK_CONVERSION_OBELISKTOSIMULATION_H
