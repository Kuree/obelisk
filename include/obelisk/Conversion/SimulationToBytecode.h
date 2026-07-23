//===- SimulationToBytecode.h - Design-wide bytecode encoder ---*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOBYTECODE_H
#define OBELISK_CONVERSION_SIMULATIONTOBYTECODE_H

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>

namespace obelisk {

struct SimulationBytecodeOptions {
  /// auto uses the design compute-graph profile; otherwise off/read/full.
  std::string vpi = "auto";
  /// Require the runtime scheduler to execute all process entries as bytecode.
  bool requireBytecode = false;
};

struct SimulationBytecodeFunction {
  std::string symbol;
  uint32_t index = 0;
  uint64_t scratchSize = 0;
  uint64_t scratchAlignment = 8;
  /// Physical scratch registers whose proven-zero unknown plane was omitted.
  uint32_t twoStateLogicRegisters = 0;
};

struct EncodedSimulationDesign {
  llvm::SmallVector<uint8_t> bytecode;
  llvm::SmallVector<uint8_t> designDatabase;
  llvm::SmallVector<SimulationBytecodeFunction> functions;
  uint64_t stateBitCount = 0;
  uint32_t executionFlags = 0;
};

/// Encode without mutating the supplied design. This API accepts the closed,
/// normalized operation set documented by EncodeObeliskSimToBytecodePass:
/// integer arith (constant, basic arithmetic/bitwise/shifts, cmp/select, and
/// integer width/index casts), cf branch/cond_br/switch, and the explicitly
/// implemented obelisk_sim executable families. Diagnostics are emitted on the
/// first operation outside that set; dialect membership alone never implies
/// bytecode legality.
mlir::FailureOr<EncodedSimulationDesign>
encodeSimulationDesign(sim::SimDesignOp design,
                       const SimulationBytecodeOptions &options = {});

} // namespace obelisk

#endif // OBELISK_CONVERSION_SIMULATIONTOBYTECODE_H
