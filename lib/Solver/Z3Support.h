#ifndef OBELISK_SOLVER_Z3SUPPORT_H
#define OBELISK_SOLVER_Z3SUPPORT_H

#ifdef OBELISK_Z3_SINGLE_THREADED
#include <mutex>
#endif

namespace obelisk::solver::detail {

#ifdef OBELISK_Z3_SINGLE_THREADED
/// One process-wide gate for the non-pthread wasm compiler's Z3 build.
std::mutex &getZ3Mutex();
#endif

} // namespace obelisk::solver::detail

#endif
