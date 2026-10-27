//===- CoverageLimits.h - shared coverage resource bounds ------*- C++ -*-===//
//
// Bounds shared by the compiler, the simulation dialect verifiers and the
// runtime. The header has no generated dependencies, so IR libraries can use
// them without depending on the coverage database format.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_COVERAGE_COVERAGELIMITS_H
#define TALOS_COVERAGE_COVERAGELIMITS_H

#include <cstdint>

namespace talos::coverage {

/// Maximum finite source-occurrence domain materialized for one Clause 19.5.1
/// `with` predicate. The compiler and runtime share this exact resource
/// bound so constructor evaluation cannot create an unbounded expression
/// batch.
inline constexpr uint32_t MaxFunctionalWithCandidates = 4096;

/// Maximum value-tuple domain materialized for one Clause 19.6.1.2 cross
/// selector `with` predicate. This is an implementation resource bound in the
/// sole mutable model.
inline constexpr uint32_t MaxFunctionalCrossWithCandidates = 4096;

} // namespace talos::coverage

#endif // TALOS_COVERAGE_COVERAGELIMITS_H
