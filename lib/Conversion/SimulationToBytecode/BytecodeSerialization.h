//===- BytecodeSerialization.h - Bytecode image serialization -*- C++ -*-===//
//
// Private, target-independent helpers shared by bytecode image emitters.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODESERIALIZATION_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODESERIALIZATION_H

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <optional>

namespace obelisk::bytecode {

inline void append16(llvm::SmallVectorImpl<uint8_t> &output, uint16_t value) {
  output.push_back(static_cast<uint8_t>(value));
  output.push_back(static_cast<uint8_t>(value >> 8));
}

inline void append32(llvm::SmallVectorImpl<uint8_t> &output, uint32_t value) {
  for (unsigned byte = 0; byte != 4; ++byte)
    output.push_back(static_cast<uint8_t>(value >> (byte * 8)));
}

inline void append64(llvm::SmallVectorImpl<uint8_t> &output, uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    output.push_back(static_cast<uint8_t>(value >> (byte * 8)));
}

inline void write32(llvm::SmallVectorImpl<uint8_t> &output, uint64_t offset,
                    uint32_t value) {
  for (unsigned byte = 0; byte != 4; ++byte)
    output[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}

inline void write64(llvm::SmallVectorImpl<uint8_t> &output, uint64_t offset,
                    uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    output[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}

inline void alignTo(llvm::SmallVectorImpl<uint8_t> &output,
                    uint64_t alignment) {
  while (output.size() % alignment != 0)
    output.push_back(0);
}

inline uint64_t checksum(llvm::ArrayRef<uint8_t> data,
                         uint64_t checksumOffset) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (auto [index, byte] : llvm::enumerate(data)) {
    uint8_t hashed =
        index >= checksumOffset && index < checksumOffset + 8 ? 0 : byte;
    hash ^= hashed;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

inline uint64_t stableHash(llvm::StringRef text) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : text.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

inline bool containsLogic(mlir::Type type) {
  return analysis::containsFourStateLogic(type);
}

inline std::optional<uint32_t> simulationWidth(mlir::Type type) {
  if (mlir::isa<sim::CovergroupHandleType>(type) ||
      sim::isManagedHandleType(type))
    return 64;
  if (std::optional<unsigned> packed = sim::getPackedWidth(type))
    return *packed;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (auto unionType = mlir::dyn_cast<sim::UnpackedUnionType>(type);
      unionType && unionType.getIsTagged() && span) {
    uint64_t tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
    if (tagBits > UINT64_MAX - *span)
      return std::nullopt;
    *span += tagBits;
  }
  if (!span || *span == 0 || *span > UINT32_MAX)
    return std::nullopt;
  return static_cast<uint32_t>(*span);
}

llvm::SmallVector<uint8_t> serializeDesignDatabase(
    sim::SimDesignOp design, uint32_t profile,
    const llvm::DenseMap<uint64_t, uint64_t> &storageOffsets,
    const llvm::DenseMap<uint64_t, uint64_t> &netOffsets,
    const llvm::DenseMap<uint64_t, uint64_t> &driverOffsets);

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODESERIALIZATION_H
