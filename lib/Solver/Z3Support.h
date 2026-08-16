#ifndef OBELISK_SOLVER_Z3SUPPORT_H
#define OBELISK_SOLVER_Z3SUPPORT_H

#include <mutex>

namespace obelisk::solver::detail {

/// One process-wide gate for the compiler's Z3_SINGLE_THREADED build.
std::mutex &getZ3Mutex();

} // namespace obelisk::solver::detail

#endif
