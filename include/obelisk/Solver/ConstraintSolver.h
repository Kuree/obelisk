//===- ConstraintSolver.h - Compiler-side constraint analysis -*- C++ -*-===//

#ifndef OBELISK_SOLVER_CONSTRAINTSOLVER_H
#define OBELISK_SOLVER_CONSTRAINTSOLVER_H

#include <cstddef>
#include <cstdint>

namespace obelisk::solver {

enum class Satisfiability { Unknown, Satisfiable, Unsatisfiable };

struct RandomProgramAnalysis {
  Satisfiability satisfiability = Satisfiability::Unknown;
  const char *backend = "heuristic";
};

/// Analyze a versioned runtime random-constraint program. Dynamic captures
/// remain free bit-vector variables, so an Unsatisfiable result proves that no
/// runtime capture values can make the hard constraints satisfiable. The API
/// deliberately exposes no Z3 types.
RandomProgramAnalysis analyzeRandomProgram(const uint8_t *program,
                                           size_t programSize,
                                           uint64_t resourceLimit = 100000);

} // namespace obelisk::solver

#endif
