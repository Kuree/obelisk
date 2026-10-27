//===- BytecodeRegisterPlanning.h - Bytecode register planning -*- C++ -*-===//
//
// Private analysis-backed planning for bytecode scratch registers.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H
#define TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H

#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/DenseSet.h"

namespace talos::bytecode {

mlir::FailureOr<llvm::DenseSet<mlir::Value>>
planTwoStateRegisters(sim::SimDesignOp design);

} // namespace talos::bytecode

#endif // TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEREGISTERPLANNING_H
