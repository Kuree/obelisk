//===- ConstraintSolver.h - Compiler-side constraint analysis -*- C++ -*-===//

#ifndef OBELISK_SOLVER_CONSTRAINTSOLVER_H
#define OBELISK_SOLVER_CONSTRAINTSOLVER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace obelisk::solver {

enum class Satisfiability { Unknown, Satisfiable, Unsatisfiable };

/// An inclusive unsigned interval containing every value of one serialized
/// random variable that can participate in a hard-constraint solution.
/// Variables are identified by their bit range in the aggregate assignment.
struct RandomVariableDomain {
  uint32_t offset = 0;
  uint32_t width = 0;
  uint64_t lower = 0;
  uint64_t upper = 0;
};

/// A compile-time-proven equality between two serialized random variables.
/// Generated proposals sample `sourceOffset` and copy it to `targetOffset`.
struct RandomVariableAlias {
  uint32_t targetOffset = 0;
  uint32_t sourceOffset = 0;
  uint32_t width = 0;
};

/// A compile-time-proven assignment of one serialized random variable from a
/// contiguous RPN expression in the analyzed runtime program. Instruction
/// indices exclude the program header and `expressionEnd` is exclusive.
struct RandomVariableDefinition {
  uint32_t targetOffset = 0;
  uint32_t width = 0;
  uint32_t expressionBegin = 0;
  uint32_t expressionEnd = 0;
};

struct RandomProgramAnalysis {
  Satisfiability satisfiability = Satisfiability::Unknown;
  const char *backend = "heuristic";
  std::vector<RandomVariableDomain> domains;
  std::vector<RandomVariableAlias> aliases;
  std::vector<RandomVariableDefinition> definitions;
  /// True only when the domain, alias, and definition proposal (with full
  /// domains for omitted variables) is equivalent to the hard formula.
  bool proposalExact = false;
};

/// Analyze a versioned runtime random-constraint program. Dynamic captures
/// remain free bit-vector variables, so an Unsatisfiable result proves that no
/// runtime capture values can make the hard constraints satisfiable. Reported
/// variable domains conservatively enclose the projection of all hard
/// solutions, including every possible runtime capture value. Aliases and
/// definitions are equalities implied by every hard solution. The API
/// deliberately exposes no Z3 types.
RandomProgramAnalysis analyzeRandomProgram(const uint8_t *program,
                                           size_t programSize,
                                           uint64_t resourceLimit = 100000);

} // namespace obelisk::solver

#endif
