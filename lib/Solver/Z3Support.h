#ifndef TALOS_SOLVER_Z3SUPPORT_H
#define TALOS_SOLVER_Z3SUPPORT_H

#include "talos/Solver/ConstraintSolver.h"

#ifdef TALOS_Z3_SINGLE_THREADED
#include <mutex>
#endif

namespace talos::solver::detail {

#ifdef TALOS_ENABLE_Z3
Satisfiability
checkRandomDomainOverlapZ3(llvm::ArrayRef<RandomDomainPattern> patterns,
                           uint64_t resourceLimit);
#endif

#ifdef TALOS_Z3_SINGLE_THREADED
/// One process-wide gate for the non-pthread wasm compiler's Z3 build.
std::mutex &getZ3Mutex();
#endif

} // namespace talos::solver::detail

#endif
