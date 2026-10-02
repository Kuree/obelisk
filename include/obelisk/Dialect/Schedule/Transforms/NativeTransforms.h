#ifndef OBELISK_DIALECT_SCHEDULE_TRANSFORMS_NATIVE_TRANSFORMS_H
#define OBELISK_DIALECT_SCHEDULE_TRANSFORMS_NATIVE_TRANSFORMS_H
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/DenseMap.h"
#include <cstdint>
namespace mlir {
class ModuleOp;
class Operation;
}
namespace obelisk::sim {
class SimDesignOp;
}
namespace obelisk::detail {
struct NativeStateLayout;
void annotateCompactNBAMetadata(mlir::ModuleOp module);
void materializeCleanEvalBodies(sim::SimDesignOp design);
mlir::LogicalResult materializeEvalTwoStateVariants(
    mlir::ModuleOp module, sim::SimDesignOp design,
    const NativeStateLayout &stateLayout, bool enabled,
    const llvm::DenseMap<uint64_t, uint32_t> &actorSlots);
} // namespace obelisk::detail
namespace obelisk::schedule {
// Function-local executable scheduling rewrites; no fallback/frame facts are
// recomputed by these actions.
void coalesceNativeReadyUpdates(mlir::Operation *operation);
void cacheNativePureCones(mlir::ModuleOp module, uint64_t minimumCost = 256,
                          uint64_t maximumCost = 4096,
                          unsigned maximumCaches = 128);
} // namespace obelisk::schedule
#endif
