#ifndef OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_H
#define OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_H
#include "obelisk/Conversion/Passes.h"
namespace obelisk {
/// Reuse only a graph known current after body fusion; not a textual option.
std::unique_ptr<mlir::Pass> createObeliskSimBuildCurrentComputeGraphPass(
    ObeliskSimBuildComputeGraphPassOptions options);
/// Canonical conversion entry point, retaining existing individual pass flags.
void registerSimulationToSchedulePipeline();
} // namespace obelisk
#endif
