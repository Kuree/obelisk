//===- UnknownBitGeometry.h - Bit masks over packed values ------*- C++ -*-===//
//
// Shared geometry of bit-precise X/Z facts: which values get a mask, how
// wide it is, and where an aggregate's subelements sit in it. Packed values
// and nested unpacked arrays of packed leaves up to `maximumDenseArrayBits`
// get one bit per bit at their provenance offsets; a larger array, a
// memory, shares one mask for all its packed leaves.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_UNKNOWNBITGEOMETRY_H
#define OBELISK_ANALYSIS_UNKNOWNBITGEOMETRY_H

#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/Types.h"
#include "llvm/ADT/APInt.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace obelisk::analysis::bits {

/// Values and states wider than this have no bit-precise fact.
constexpr unsigned maximumTrackedBits = 1u << 16;

/// Nested unpacked arrays of packed leaves up to this many bits are tracked
/// bit by bit; larger ones share one mask for all their leaves.
constexpr uint64_t maximumDenseArrayBits = 4096;

/// The bit width of a packed value's scalar representation.
inline std::optional<unsigned> scalarWidth(mlir::Type type) {
  if (std::optional<unsigned> width = sim::getPackedWidth(type))
    return width;
  if (mlir::Type scalar = sim::getPackedScalarType(type))
    return sim::getPackedWidth(scalar);
  return std::nullopt;
}

/// A type itself, or the innermost element of nested unpacked arrays.
inline mlir::Type packedLeaf(mlir::Type type) {
  while (auto array = mlir::dyn_cast<sim::UnpackedArrayType>(type))
    type = array.getElementType();
  return type;
}

/// Whether values and storage of `type` share one mask for all the packed
/// leaves of its nested unpacked arrays.
inline bool isElementwise(mlir::Type type) {
  if (!mlir::isa<sim::UnpackedArrayType>(type))
    return false;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  return !span || *span > maximumDenseArrayBits;
}

/// The width of a value's mask: a packed value's scalar width, a small
/// array's provenance span, or the packed leaf's width for a larger array.
inline std::optional<unsigned> maskWidth(mlir::Type type) {
  std::optional<unsigned> leaf = scalarWidth(packedLeaf(type));
  if (!leaf || !mlir::isa<sim::UnpackedArrayType>(type) || isElementwise(type))
    return leaf;
  return static_cast<unsigned>(*sim::getProvenanceSpan(type));
}

/// The X/Z bits of the default value of `type` (IEEE 1800-2023 6.8, Table
/// 6-7; 7.4.5, Table 7-1), or of its packed leaf for a shared mask.
inline llvm::APInt defaultUnknown(mlir::Type type, unsigned width) {
  return getDefaultUnknownMask(isElementwise(type) ? packedLeaf(type) : type)
      .zextOrTrunc(width);
}

/// Subelement `index` of packed `type` as (offset, width) when it fits
/// `width` bits.
inline std::optional<std::pair<unsigned, unsigned>>
subelementPlacement(mlir::Type type, uint64_t index, unsigned width) {
  auto found =
      sim::getAggregateProvenanceSubelement(type, static_cast<unsigned>(index));
  if (!found || found->first > width || found->second > width - found->first)
    return std::nullopt;
  return std::pair<unsigned, unsigned>(found->first, found->second);
}

} // namespace obelisk::analysis::bits

#endif // OBELISK_ANALYSIS_UNKNOWNBITGEOMETRY_H
