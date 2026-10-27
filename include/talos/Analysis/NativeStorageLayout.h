//===- NativeStorageLayout.h - LLVM aggregate storage geometry --*- C++ -*-===//

#ifndef TALOS_ANALYSIS_NATIVESTORAGELAYOUT_H
#define TALOS_ANALYSIS_NATIVESTORAGELAYOUT_H

#include "talos/Dialect/Simulation/SimulationAttrs.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

#include <optional>

namespace talos::analysis {

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

/// The maximal runs of getLogicalStorageMask as (low bit, width), in order,
/// or nullopt past `limit` runs. The work is bounded by `limit`, not by the
/// width of the type.
std::optional<llvm::SmallVector<std::pair<uint64_t, uint64_t>>>
getLogicalStorageRuns(mlir::Type type, size_t limit);

/// The storage planes of a type's default value (IEEE 1800-2023 Table 6-7 and
/// Table 7-1). Unlike getDefaultUnknownMask, which only marks four-state
/// bits, these carry the constant member defaults of unpacked structs
/// (7.2.2): an initialized member holds its initializer's bits instead.
struct DefaultStoragePlanes {
  llvm::APInt value;
  llvm::APInt unknown;
};
DefaultStoragePlanes getDefaultStoragePlanes(mlir::Type type);

/// Whether the fixed storage of `type` holds a declared struct member default.
bool hasMemberDefaults(mlir::Type type);

/// Reject stored layout encodings from a different target before either
/// execution backend consumes their physical offsets.
mlir::LogicalResult verifyNativeStorageLayouts(mlir::Operation *root,
                                              const llvm::DataLayout &layout);

} // namespace talos::analysis
#endif
