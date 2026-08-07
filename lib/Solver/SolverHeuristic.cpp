#include "obelisk/Solver/ConstraintSolver.h"

namespace obelisk::solver {

#ifndef OBELISK_ENABLE_Z3
RandomProgramAnalysis analyzeRandomProgram(const uint8_t *, size_t, uint64_t) {
  return {};
}
#endif

} // namespace obelisk::solver
