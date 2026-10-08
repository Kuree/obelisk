//===- PackedStorageLayout.h - Packed access units ------------*- C++ -*-===//

#ifndef OBELISK_CONVERSION_PACKEDSTORAGELAYOUT_H
#define OBELISK_CONVERSION_PACKEDSTORAGELAYOUT_H

#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <cstdint>
#include <optional>

namespace llvm {
class DataLayout;
}
namespace mlir {
class ModuleOp;
}
namespace obelisk::detail {

/// The containing memory access, independent of the logical selected bits.
/// Sibling fields use the same address and integer type, as in Clang's
/// CGBitFieldInfo. Stores preserve the other bits in this unit.
struct PackedAccessUnit {
  unsigned bitOffset;
  unsigned width;
  // A complete unpacked element owns its slot's tail padding. A packed field
  // shares its unit with siblings and must preserve all non-field bits.
  bool ownsPadding = false;
};

class PackedStorageLayout {
public:
  PackedStorageLayout(mlir::ModuleOp module, const llvm::DataLayout &layout);

  /// Select a Clang-style access unit for a packed record inside canonical
  /// bitstream storage. physicalOffset is the containing object's bit address.
  std::optional<PackedAccessUnit> get(mlir::Type type, uint64_t physicalOffset,
                                      uint64_t offset, unsigned width) const;
  /// A typed field path, relative to a byte-addressable record. Its reference
  /// is a whole record or invalid, never a clipped selection; lowering must
  /// retain its index guards. The record's access unit is fixed.
  std::optional<PackedAccessUnit>
  get(mlir::Value reference,
      llvm::function_ref<bool(mlir::Value)> isByteAddressable) const;

private:
  unsigned registerBytes;
  bool cheapUnaligned;
  bool littleEndian;
};

} // namespace obelisk::detail
#endif
