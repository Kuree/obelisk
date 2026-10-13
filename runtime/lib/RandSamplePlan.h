//===- RandSamplePlan.h - Specialize and sample a compiled relation
//--------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct obelisk_rt_random_state_v1;

namespace obelisk::runtime {
enum class SamplerResult { Unavailable, Satisfiable, Unsatisfiable, Invalid };

/// Tentative history for one cyclic stage. The caller owns persistence and
/// publishes the next history only after every randomization stage succeeds.
struct CyclicSampleRequest {
  uint32_t offset, width;
  uint64_t key, position;
  const uint8_t *previousDomain;
  size_t previousDomainSize;
  std::vector<uint8_t> domain;
};

/// Sample the compiler-generated plan after binding state and frozen inputs.
/// Exact arbitrary-precision counts support uniform and weighted projections.
/// Failure commits neither assignments nor object-local RNG state.
SamplerResult sampleRandomPlan(
    const uint8_t *program, size_t programSize, const uint64_t *start,
    const uint64_t *mutableMask, size_t assignmentWords,
    uint64_t constraintMask, const uint64_t *captures,
    const uint32_t *captureWidths, size_t captureCount, uint64_t *assignment,
    obelisk_rt_random_state_v1 *randomState = nullptr,
    const uint64_t *orderedMasks = nullptr, size_t orderedLayerCount = 0,
    CyclicSampleRequest *cyclic = nullptr);
} // namespace obelisk::runtime
