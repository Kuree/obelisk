//===- RandSolveSMT.h - Runtime constraint completion
//----------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

namespace obelisk::runtime {

enum class SMTCompletion { Unavailable, Satisfiable, Unsatisfiable, Unknown };

/// Complete a validated integral residual program after bounded traversal.
/// Captures and inactive bits are concrete, and constraint modes are sampled
/// by the caller. The result follows the residual's unsigned cyclic order;
/// this does not claim uniform sampling of arbitrary joint solution spaces.
SMTCompletion
completeRandomProgram(const uint8_t *program, size_t programSize,
                      const uint64_t *start, const uint64_t *mutableMask,
                      size_t assignmentWords, uint64_t constraintMask,
                      const uint64_t *captures, const uint32_t *captureWidths,
                      size_t captureCount, uint64_t *assignment);

} // namespace obelisk::runtime
