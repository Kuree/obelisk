//===- RandSolveSMT.h - Runtime constraint completion
//----------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

struct obelisk_rt_random_state_v1;

namespace obelisk::runtime {

enum class SMTCompletion { Unavailable, Satisfiable, Unsatisfiable, Unknown };

/// Complete a validated integral residual program after bounded traversal.
/// Captures and inactive bits are concrete, and constraint modes are sampled
/// by the caller. The result follows the residual's unsigned cyclic order;
/// SMT_UNIFORM instead samples the complete finite set of legal assignments
/// with exact cardinalities and updates the supplied object-local stream.
/// Ordered masks select conditional projections before later fields are solved.
SMTCompletion completeRandomProgram(
    const uint8_t *program, size_t programSize, const uint64_t *start,
    const uint64_t *mutableMask, size_t assignmentWords,
    uint64_t constraintMask, const uint64_t *captures,
    const uint32_t *captureWidths, size_t captureCount, uint64_t *assignment,
    obelisk_rt_random_state_v1 *randomState = nullptr,
    const uint64_t *orderedMasks = nullptr, size_t orderedLayerCount = 0);

} // namespace obelisk::runtime
