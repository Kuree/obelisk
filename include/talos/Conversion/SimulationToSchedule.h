#ifndef TALOS_CONVERSION_SIMULATIONTOSCHEDULE_H
#define TALOS_CONVERSION_SIMULATIONTOSCHEDULE_H
#include "talos/Conversion/Passes.h"
namespace talos {
/// Reuse only a graph known current after body fusion; not a textual option.
std::unique_ptr<mlir::Pass> createTalosSimBuildCurrentComputeGraphPass(
    TalosSimBuildComputeGraphPassOptions options);
/// Canonical conversion entry point, retaining existing individual pass flags.
void registerSimulationToSchedulePipeline();
} // namespace talos
#endif
