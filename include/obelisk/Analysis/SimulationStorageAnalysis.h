//===- SimulationStorageAnalysis.h - Canonical storage facts ---*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_SIMULATIONSTORAGEANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONSTORAGEANALYSIS_H

#include "mlir/IR/Types.h"
#include "mlir/Support/LLVM.h"

#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include <cstddef>
#include <cstdint>

namespace llvm {
class DataLayout;
class LLVMContext;
} // namespace llvm

namespace obelisk::analysis {

/// Target-dependent physical storage used by native and bytecode simulation
/// state. Four-state values use two planes; managed references use an object
/// word followed by a byte-offset word.
struct SimulationStorageProperties {
  uint64_t size;
  uint32_t alignment;
  bool fourState;
  bool managedReference;
  llvm::SmallVector<sim::ManagedHandleSlot, 2> managedRootSlots;
  llvm::SmallVector<uint64_t, 2> managedRootOffsets;
  uint64_t managedRootSize;
  uint32_t managedRootAlignment;
};

mlir::FailureOr<SimulationStorageProperties>
getSimulationStorageProperties(mlir::Type type,
                               const llvm::DataLayout &dataLayout,
                               llvm::LLVMContext &llvmContext);

size_t
getSimulationPhysicalStorageCount(const SimulationStorageProperties &storage);

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_SIMULATIONSTORAGEANALYSIS_H
