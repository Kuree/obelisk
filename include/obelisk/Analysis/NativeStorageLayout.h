//===- NativeStorageLayout.h - LLVM aggregate storage geometry --*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_NATIVESTORAGELAYOUT_H
#define OBELISK_ANALYSIS_NATIVESTORAGELAYOUT_H

#include "obelisk/Dialect/Simulation/SimulationAttrs.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

namespace obelisk::analysis {

/// One worker's LLVM type and DataLayout caches. Packed values are single iN
/// leaves; only unpacked arrays and records introduce LLVM aggregate layout.
class NativeStorageLayout {
public:
  explicit NativeStorageLayout(const llvm::DataLayout &layout)
      : dataLayout(layout) {}
  llvm::Type *getType(mlir::Type type);
  sim::StorageLayoutAttr getEncoding(mlir::Type type);
  const llvm::DataLayout &getDataLayout() const { return dataLayout; }

private:
  llvm::LLVMContext context;
  llvm::DataLayout dataLayout;
  llvm::DenseMap<mlir::Type, llvm::Type *> types;
};

/// Attach target-derived geometry before any executable handles or bit-stream
/// plans are built. A target-independent module retains its existing layout.
mlir::FailureOr<mlir::Type> encodeNativeStorageLayout(mlir::Type type,
                                                   mlir::Operation *scope);

/// Default X bits of an aggregate, excluding two-state fields and LLVM padding.
/// Padding is always initialized to zero in both execution tiers.
llvm::APInt getDefaultUnknownMask(mlir::Type type);

/// Bits belonging to language values, excluding target ABI padding.
llvm::APInt getLogicalStorageMask(mlir::Type type);

/// Reject stored layout encodings from a different target before either
/// execution backend consumes their physical offsets.
mlir::LogicalResult verifyNativeStorageLayouts(mlir::Operation *root,
                                              const llvm::DataLayout &layout);

} // namespace obelisk::analysis
#endif
