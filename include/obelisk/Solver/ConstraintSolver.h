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

struct RandomProgramAnalysis {
  Satisfiability satisfiability = Satisfiability::Unknown;
  const char *backend = "heuristic";
  std::vector<RandomVariableDomain> domains;
  /// True only when the Cartesian product of `domains` (and full domains for
  /// omitted variables) is equivalent to the hard constraint formula.
  bool proposalExact = false;
};

/// Analyze a versioned runtime random-constraint program. Dynamic captures
/// remain free bit-vector variables, so an Unsatisfiable result proves that no
/// runtime capture values can make the hard constraints satisfiable. Reported
/// variable domains conservatively enclose the projection of all hard
/// solutions, including every possible runtime capture value. The API
/// deliberately exposes no Z3 types.
RandomProgramAnalysis analyzeRandomProgram(const uint8_t *program,
                                           size_t programSize,
                                           uint64_t resourceLimit = 100000);

} // namespace obelisk::solver

#endif
