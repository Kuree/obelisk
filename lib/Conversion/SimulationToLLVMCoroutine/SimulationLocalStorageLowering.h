//===- SimulationLocalStorageLowering.h -------------------------*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONLOCALSTORAGELOWERING_H
#define OBELISK_CONVERSION_SIMULATIONLOCALSTORAGELOWERING_H

#include "PackedStorageLayout.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class DataLayout;
}
namespace mlir {
class RewritePatternSet;
class TypeConverter;
} // namespace mlir
namespace obelisk::detail {

/// Per-conversion storage plan. Local reference values lower to bit offsets;
/// the closed-use proof supplies their stack base without a runtime handle.
class LocalStorageLowering {
public:
  struct Slot {
    mlir::Value value;
    mlir::Value unknown;
    unsigned storageWidth;
    bool nativeLayout;
  };
  struct Layout {
    mlir::Type type;
    uint64_t bytes;
    unsigned alignment;
    llvm::SmallVector<uint64_t> elementOffsets;
  };
  LocalStorageLowering(llvm::ArrayRef<mlir::Operation *> functions,
                       const llvm::DataLayout &dataLayout,
                       const llvm::DenseSet<mlir::Value> &byteAddressableState);
  bool isWholeAllocation(mlir::Value reference) const {
    return wholeAllocations.contains(reference);
  }
  /// A complete native element owns its allocation padding. Packed field
  /// views and references crossing unpacked elements do not have this proof.
  bool isNativeObject(mlir::Value reference) const {
    return nativeObjects.contains(reference);
  }
  bool isMemoryValue(mlir::Value value) const {
    return memoryValues.contains(value);
  }
  bool canBorrowMemory(mlir::Value value) const {
    return borrowedValues.contains(value);
  }
  bool usesNativeMemoryLayout(mlir::Value value) const {
    return nativeMemoryValues.contains(value);
  }
  void addValueConversions(mlir::TypeConverter &converter) const;
  const Layout &getLayout(mlir::Type type) const {
    return layouts.find(type)->second;
  }
  uint64_t getElementOffset(mlir::Value reference, mlir::Type type,
                            unsigned index) const;
  uint64_t getArrayStride(mlir::Value reference) const;
  std::optional<PackedAccessUnit> getAccessUnit(mlir::Value reference) const {
    auto found = accessUnits.find(reference);
    return found == accessUnits.end() ? std::nullopt
                                      : std::optional(found->second);
  }

  const Slot *lookup(mlir::Value reference) const {
    auto found = references.find(reference);
    return found == references.end() ? nullptr : &slots[found->second];
  }

private:
  llvm::DenseSet<mlir::Value> wholeAllocations;
  llvm::DenseSet<mlir::Value> nativeObjects;
  llvm::DenseSet<mlir::Value> memoryValues;
  llvm::DenseSet<mlir::Value> borrowedValues;
  llvm::DenseSet<mlir::Value> nativeMemoryValues;
  llvm::DenseMap<mlir::Type, Layout> layouts;
  llvm::DenseMap<mlir::Value, unsigned> references;
  llvm::DenseMap<mlir::Value, PackedAccessUnit> accessUnits;
  llvm::SmallVector<Slot> slots;
};

void populateLocalStorageConversionPatterns(mlir::RewritePatternSet &patterns,
                                            mlir::TypeConverter &converter,
                                            const LocalStorageLowering &locals);
} // namespace obelisk::detail
#endif
