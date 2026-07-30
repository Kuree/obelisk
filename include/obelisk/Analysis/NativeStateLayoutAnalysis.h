//===- NativeStateLayoutAnalysis.h - Stable native state layout -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_NATIVESTATELAYOUTANALYSIS_H
#define OBELISK_ANALYSIS_NATIVESTATELAYOUTANALYSIS_H

#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <utility>

namespace obelisk::analysis {

/// Stable root allocation and connectivity facts for native simulation state.
struct NativeStateLayoutAnalysis {
  struct Bound {
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    bool fourState;
    mlir::SmallVector<uint64_t, 2> managedRootOffsets;
  };
  struct Net {
    uint64_t id;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    bool fourState;
    sim::NetResolutionKind resolution;
  };
  struct Driver {
    uint64_t id;
    uint64_t netId;
    uint32_t handleID;
    uint64_t offset;
    unsigned width;
    unsigned drivenLow;
    unsigned drivenWidth;
  };

  static mlir::FailureOr<NativeStateLayoutAnalysis>
  compute(mlir::ModuleOp module);

  llvm::DenseMap<uint64_t, uint64_t> storage;
  llvm::DenseMap<uint64_t, uint64_t> nets;
  llvm::DenseMap<uint64_t, uint64_t> drivers;
  llvm::DenseMap<uint64_t, uint64_t> storageOffsets;
  llvm::DenseMap<uint64_t, uint64_t> netOffsets;
  llvm::DenseMap<uint64_t, uint64_t> driverOffsets;
  mlir::SmallVector<Bound> bounds;
  mlir::SmallVector<Net> netLayouts;
  mlir::SmallVector<Driver> driverLayouts;
  llvm::DenseMap<std::pair<uint64_t, uint64_t>,
                 std::pair<uint64_t, uint64_t>>
      connectivityCanonical;
  llvm::DenseMap<std::pair<uint64_t, uint64_t>,
                 mlir::SmallVector<NetBit>>
      connectivityComponents;
  uint64_t bitCount = 0;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_NATIVESTATELAYOUTANALYSIS_H
