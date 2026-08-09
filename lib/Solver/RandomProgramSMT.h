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

enum class SMTCaptureBoundKind {
  LowerInclusive,
  LowerExclusive,
  UpperInclusive,
  UpperExclusive,
};

struct SMTVariableCaptureBound {
  SMTVariable target;
  uint32_t captureIndex;
  SMTCaptureBoundKind kind;
  bool isSigned;
  mlir::Value predicate;
};

struct SMTVariableDefinition {
  SMTVariable target;
  mlir::Value expression;
  uint32_t expressionBegin;
  uint32_t expressionEnd;
  std::vector<SMTVariable> dependencies;
};

struct SMTHardConstraint {
  mlir::Value expression;
  std::vector<SMTVariable> dependencies;
  bool hasCapture = false;
};

/// An exact semantic-domain predicate carried by the runtime program. These
/// predicates are part of the hard formula, but are also retained separately
/// so compile-time proposal analysis can reproduce the compiler-emitted finite
/// domain instead of approximating it with one enclosing interval.
struct SMTFiniteDomain {
  SMTVariable target;
  mlir::Value predicate;
};

/// Owns a temporary, verified SMT-dialect module. The context must outlive the
/// module and all values retained from it, hence the declaration order.
struct RandomProgramSMT {
  std::unique_ptr<mlir::MLIRContext> context;
  mlir::OwningOpRef<mlir::ModuleOp> module;
  mlir::smt::SolverOp solver;
  mlir::Value assignment;
  std::vector<mlir::Value> captures;
  mlir::Value hard;
  std::vector<SMTVariable> variables;
  std::vector<SMTHardConstraint> hardConstraints;
  std::vector<SMTFiniteDomain> finiteDomains;
  std::vector<SMTVariableCaptureBound> directCaptureBounds;
  std::vector<SMTVariableEquality> directEqualities;
  std::vector<SMTVariableDefinition> directDefinitions;
};

std::optional<RandomProgramSMT> buildRandomProgramSMT(const uint8_t *program,
                                                      size_t programSize);

} // namespace obelisk::solver

#endif
