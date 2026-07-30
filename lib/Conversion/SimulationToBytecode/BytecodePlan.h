//===- BytecodePlan.h - Shared bytecode encoder plan records -*- C++ -*-===//
//
// Private plan records shared by instruction selection and image emission.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEPLAN_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEPLAN_H

#include "BytecodeLayout.h"
#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"

#include "mlir/Analysis/Liveness.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace obelisk::bytecode {

struct Instruction {
  uint16_t opcode = OBELISK_RT_DB_NOP;
  uint16_t flags = 0;
  uint32_t destination = 0;
  uint32_t source0 = 0;
  uint32_t source1 = 0;
  uint32_t source2 = 0;
  uint32_t auxiliary = 0;
  uint64_t immediate = 0;
};

struct OperandMap {
  uint32_t destination;
  uint32_t source;
};

struct Continuation {
  uint32_t function;
  uint32_t id;
  uint64_t instruction;
  uint32_t scheduleRank;
};

struct IntrinsicSignature {
  uint32_t id;
  uint32_t inputCount;
  uint32_t outputCount;
  uint32_t flags;
};

struct IntrinsicSite {
  uint32_t intrinsic;
  uint32_t firstOperand;
  uint32_t inputCount;
  uint32_t outputCount;
};

struct CaptureRecord {
  uint32_t function;
  uint32_t argument;
  uint64_t valueOffset;
  uint64_t unknownOffset;
  uint64_t planeSize;
};

struct FunctionPlan {
  sim::SimFuncOp function;
  uint32_t index = 0;
  uint64_t stableID = 0;
  llvm::MapVector<mlir::Value, uint32_t> registers;
  llvm::SmallVector<Layout> layouts;
  llvm::SmallVector<uint32_t> resultRegisters;
  llvm::DenseMap<mlir::Block *, uint64_t> blockPCs;
  llvm::SmallVector<std::pair<uint64_t, mlir::Block *>> branches;
  llvm::SmallVector<Continuation> continuations;
  std::unique_ptr<SimulationProcessFrameAnalysis> frame;
  uint64_t firstInstruction = 0;
  uint64_t instructionCount = 0;
  uint64_t scratchSize = 0;
  uint64_t scratchAlignment = 8;
  uint32_t twoStateLogicRegisters = 0;
  uint32_t initialScheduleRank = UINT32_MAX;
  llvm::DenseMap<mlir::Block *, uint32_t> blockScheduleRanks;
  std::unique_ptr<mlir::Liveness> liveness;
  struct ManagedRootShadow {
    mlir::Value value;
    uint64_t bitOffset;
    uint32_t reg;
  };
  llvm::SmallVector<ManagedRootShadow> managedRootShadows;
};

llvm::SmallVector<uint8_t> serializeBytecodeImage(
    llvm::MutableArrayRef<FunctionPlan> plans,
    llvm::ArrayRef<Instruction> instructions,
    llvm::ArrayRef<OperandMap> operandMaps, llvm::ArrayRef<uint8_t> constants,
    llvm::ArrayRef<IntrinsicSignature> intrinsicSignatures,
    llvm::ArrayRef<IntrinsicSite> intrinsicSites,
    llvm::ArrayRef<CaptureRecord> captureRecords, const StateLayout &state);

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEPLAN_H
