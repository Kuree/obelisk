//===- BytecodeLayout.h - Bytecode register and state layout -*- C++ -*-===//
//
// Private layout model shared by bytecode planning and serialization.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODELAYOUT_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODELAYOUT_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"

#include <cstdint>

namespace obelisk::bytecode {

enum RegisterKind : uint8_t {
  Invalid = OBELISK_RT_DBREG_INVALID,
  Bits = OBELISK_RT_DBREG_BITS,
  Logic = OBELISK_RT_DBREG_LOGIC,
  Handle = OBELISK_RT_DBREG_HANDLE,
  Status = OBELISK_RT_DBREG_STATUS,
  Resource = OBELISK_RT_DBREG_RESOURCE,
  Bytes = OBELISK_RT_DBREG_BYTES,
  Managed = OBELISK_RT_DBREG_MANAGED,
  ManagedRef = OBELISK_RT_DBREG_MANAGED_REF,
  ArgumentRef = OBELISK_RT_DBREG_ARGUMENT_REF,
  String = OBELISK_RT_DBREG_STRING,
  Real32 = OBELISK_RT_DBREG_REAL32,
  Real64 = OBELISK_RT_DBREG_REAL64,
};

struct Layout {
  uint8_t kind = Invalid;
  uint8_t flags = 0;
  uint32_t width = 0;
  uint64_t offset = 0;
  uint64_t size = 0;
  uint64_t auxiliary = 0;
};

inline bool isManagedAggregateWord(uint8_t kind) {
  return kind == Managed || kind == String;
}

struct ManagedValueStorage {
  uint64_t planeSize;
  uint32_t alignment;
  bool fourState;
};

struct StateLayout {
  struct Net {
    uint64_t id;
    uint64_t offset;
    uint32_t width;
    bool fourState;
    sim::NetResolutionKind resolution;
  };
  struct Driver {
    uint64_t id;
    uint64_t offset;
    uint64_t netOffset;
    uint32_t width;
    uint32_t drivenLow;
    uint32_t drivenWidth;
    sim::NetResolutionKind resolution;
  };
  struct Connection {
    uint64_t lhsOffset;
    uint64_t rhsOffset;
    uint64_t width;
    sim::NetResolutionKind lhsResolution;
    sim::NetResolutionKind rhsResolution;
    bool rhsReversed;
  };
  llvm::DenseMap<uint64_t, uint64_t> storage;
  llvm::DenseMap<uint64_t, uint64_t> nets;
  llvm::DenseMap<uint64_t, uint64_t> drivers;
  llvm::DenseMap<uint64_t, uint64_t> storageOffsets;
  llvm::DenseMap<uint64_t, uint64_t> netOffsets;
  llvm::DenseMap<uint64_t, uint64_t> driverOffsets;
  llvm::SmallVector<Net> netLayouts;
  llvm::SmallVector<Driver> driverLayouts;
  llvm::SmallVector<Connection> connections;
  uint64_t bits = 0;
};

mlir::FailureOr<Layout> getLayout(mlir::Type type);
mlir::FailureOr<ManagedValueStorage>
getManagedValueStorage(mlir::Type type, const llvm::DataLayout &dataLayout);
mlir::FailureOr<StateLayout> buildStateLayout(sim::SimDesignOp design);

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODELAYOUT_H
