//===- BytecodeRegisterPlanning.h - Bytecode register planning -*- C++ -*-===//
//
// Private analysis-backed planning for bytecode scratch registers.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/DenseSet.h"

namespace obelisk::bytecode {

mlir::FailureOr<llvm::DenseSet<mlir::Value>>
planTwoStateRegisters(sim::SimDesignOp design);

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H
