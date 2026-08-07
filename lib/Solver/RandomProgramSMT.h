//===- RandomProgramSMT.h - Random constraints as MLIR SMT ----*- C++ -*-===//

#ifndef OBELISK_LIB_SOLVER_RANDOMPROGRAMSMT_H
#define OBELISK_LIB_SOLVER_RANDOMPROGRAMSMT_H

#include "mlir/Dialect/SMT/IR/SMTOps.h"
#include "mlir/IR/BuiltinOps.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace obelisk::solver {

struct SMTVariable {
  uint32_t offset;
  unsigned width;
  mlir::Value bits;
};

struct SMTVariableEquality {
  SMTVariable lhs;
  SMTVariable rhs;
};

struct SMTVariableDefinition {
  SMTVariable target;
  mlir::Value expression;
  uint32_t expressionBegin;
  uint32_t expressionEnd;
};

/// Owns a temporary, verified SMT-dialect module. The context must outlive the
/// module and all values retained from it, hence the declaration order.
struct RandomProgramSMT {
  std::unique_ptr<mlir::MLIRContext> context;
  mlir::OwningOpRef<mlir::ModuleOp> module;
  mlir::smt::SolverOp solver;
  mlir::Value hard;
  std::vector<SMTVariable> variables;
  std::vector<SMTVariableEquality> directEqualities;
  std::vector<SMTVariableDefinition> directDefinitions;
};

std::optional<RandomProgramSMT> buildRandomProgramSMT(const uint8_t *program,
                                                      size_t programSize);

} // namespace obelisk::solver

#endif
