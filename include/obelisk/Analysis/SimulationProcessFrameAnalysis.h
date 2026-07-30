//===- SimulationProcessFrameAnalysis.h - Process frame facts -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_SIMULATIONPROCESSFRAMEANALYSIS_H
#define OBELISK_ANALYSIS_SIMULATIONPROCESSFRAMEANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <memory>

namespace llvm {
class DataLayout;
}

namespace obelisk {

enum class ProcessFrameFieldKind : uint32_t {
  Capture = 1,
  Continuation = 2,
  Live = 3,
  Wait = 4,
};

enum class ProcessFrameFieldFlags : uint32_t {
  None = 0,
  FourStateValue = 1,
  FourStateUnknown = 2,
  ManagedRoot = 4,
};

struct ProcessFrameField {
  ProcessFrameFieldKind kind;
  ProcessFrameFieldFlags flags;
  uint64_t offset;
  uint64_t size;
  uint32_t alignment;
};

struct ProcessFrameValue {
  uint64_t valueOffset;
  uint64_t unknownOffset;
  uint64_t storageSize;
  uint32_t alignment;
  uint64_t auxiliaryOffset = UINT64_MAX;
  llvm::SmallVector<uint64_t, 2> managedRootOffsets;

  bool hasValueStorage() const { return valueOffset != UINT64_MAX; }
  bool isFourState() const { return unknownOffset != UINT64_MAX; }
  bool hasAuxiliary() const { return auxiliaryOffset != UINT64_MAX; }
  bool hasManagedRoots() const { return !managedRootOffsets.empty(); }
  bool hasSecondaryStorage() const { return isFourState() || hasAuxiliary(); }
  uint64_t getSecondaryOffset() const {
    return isFourState() ? unknownOffset : auxiliaryOffset;
  }
};

struct ProcessSuspension {
  mlir::Operation *operation;
  mlir::Block *continuation;
  uint32_t continuationID;
  uint64_t waitOffset;
  uint64_t waitSize;
};

/// Target-layout-aware, deterministic canonical frame for one suspendable
/// obelisk_sim.func. The analysis records entry captures, continuation block
/// arguments, and reusable wait storage; four-state values always occupy
/// adjacent value and unknown planes.
class SimulationProcessFrameAnalysis {
public:
  static mlir::FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>>
  create(sim::SimFuncOp function, const llvm::DataLayout &dataLayout);

  uint64_t getFrameSize() const { return frameSize; }
  uint64_t getFrameAlignment() const { return frameAlignment; }
  uint64_t getChecksum() const { return checksum; }
  llvm::ArrayRef<ProcessFrameField> getFields() const { return fields; }
  llvm::ArrayRef<uint32_t> getContinuations() const { return continuations; }
  llvm::ArrayRef<ProcessFrameValue> getEntryCaptureLayout() const {
    return entryCaptureLayout;
  }
  llvm::ArrayRef<ProcessFrameValue>
  getContinuationLayout(mlir::Block *block) const;
  llvm::ArrayRef<ProcessFrameValue>
  getContinuationLayout(uint32_t continuationID) const;
  llvm::ArrayRef<ProcessSuspension> getSuspensions() const {
    return suspensions;
  }
  const ProcessSuspension *getSuspension(mlir::Operation *operation) const;

private:
  uint64_t frameSize = 0;
  uint64_t frameAlignment = 1;
  uint64_t checksum = 0;
  llvm::SmallVector<ProcessFrameField> fields;
  llvm::SmallVector<uint32_t> continuations;
  llvm::SmallVector<ProcessFrameValue> entryCaptureLayout;
  llvm::DenseMap<mlir::Block *, llvm::SmallVector<ProcessFrameValue>>
      continuationLayouts;
  llvm::DenseMap<uint32_t, llvm::SmallVector<ProcessFrameValue>>
      continuationLayoutsByID;
  llvm::SmallVector<ProcessSuspension> suspensions;
};

} // namespace obelisk

#endif // OBELISK_ANALYSIS_SIMULATIONPROCESSFRAMEANALYSIS_H
