//===- SimulationToLLVMCoroutine.h - Process coroutine lowering -*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
#define OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/DialectConversion.h"

#include <cstdint>
#include <memory>

namespace llvm {
class DataLayout;
}

namespace mlir {
class LLVMTypeConverter;
class ModuleOp;
class RewritePatternSet;
} // namespace mlir

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

  bool isFourState() const { return unknownOffset != UINT64_MAX; }
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

/// Normalize packed and suspension-live state, then analyze and construct
/// process ramps, continuation shims, descriptors, and native hooks before the
/// terminal LLVM dialect conversion.
mlir::LogicalResult
prepareSimulationProcessesToLLVMCoroutines(mlir::ModuleOp module,
                                           const llvm::DataLayout &dataLayout);

/// Add the Runtime, function, arithmetic, and control-flow patterns used after
/// coroutine process construction. Packed-value normalization is performed by
/// prepareSimulationProcessesToLLVMCoroutines. Call
/// prepareSimulationProcessesToLLVMCoroutines first when composing the full
/// process conversion outside the registered pass.
void populateSimulationCoroutineToLLVMPatterns(
    const mlir::LLVMTypeConverter &converter,
    mlir::RewritePatternSet &patterns);

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTOLLVMCOROUTINE_H
