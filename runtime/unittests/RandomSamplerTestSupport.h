// Compile residual fixtures in the test process. Compiler Z3 is a test-only
// dependency here; it is never linked into a target runtime archive.
#pragma once

#ifdef OBELISK_TEST_COMPILED_SAMPLER
#include "obelisk/Runtime/RandomSamplerPlan.h"
#include "obelisk/Solver/ConstraintSolver.h"

inline void appendTestSamplerPlan(std::vector<uint8_t> &bytes) {
  uint32_t flags = obelisk::random::planRead32(bytes.data() + 20);
  if ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN) ||
      !(flags & (OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM |
                 OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED)))
    return;
  bool wide = bytes[4] == 2;
  uint32_t captures = obelisk::random::planRead32(bytes.data() + 16);
  std::vector<uint32_t> widths(captures, 64);
  uint32_t count = obelisk::random::planRead32(bytes.data() + 12);
  if (wide)
    for (uint32_t index = 0; index != count; ++index) {
      const uint8_t *p = bytes.data() + 32 + size_t(index) * 16;
      if (p[0] == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
        uint32_t capture = obelisk::random::planRead32(p + 8);
        if (capture < captures)
          widths[capture] =
              std::max(widths[capture], obelisk::random::planRead32(p + 4));
      }
    }
  auto plan = obelisk::solver::compileRandomSamplerPlan(bytes.data(),
                                                        bytes.size(), widths);
  if (!plan)
    return; // Deliberately malformed residual fixtures still reach validation.
  bytes.insert(bytes.end(), plan->begin(), plan->end());
  flags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN;
  for (unsigned byte = 0; byte != 4; ++byte)
    bytes[20 + byte] = static_cast<uint8_t>(flags >> (byte * 8));
}
#else
inline void appendTestSamplerPlan(std::vector<uint8_t> &) {}
#endif
