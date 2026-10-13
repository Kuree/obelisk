#include "obelisk/Solver/ConstraintSolver.h"

#include "Z3Support.h"

#include <array>

namespace obelisk::solver {

Satisfiability
checkRandomDomainOverlap(llvm::ArrayRef<RandomDomainPattern> patterns,
                         uint64_t resourceLimit) {
  if (patterns.empty())
    return Satisfiability::Unsatisfiable;
  unsigned width = patterns.front().mask.getBitWidth();
  bool equalMasks = true;
  uint64_t patternWords = 0;
  for (const RandomDomainPattern &pattern : patterns) {
    if (!width || pattern.mask.getBitWidth() != width ||
        pattern.value.getBitWidth() != width ||
        !(pattern.value & ~pattern.mask).isZero())
      return Satisfiability::Unknown;
    equalMasks &= pattern.mask == patterns.front().mask;
    uint64_t words = pattern.mask.getNumWords();
    if (patternWords > UINT64_MAX - words)
      return Satisfiability::Unknown;
    patternWords += words;
  }
  if (patterns.size() < 2)
    return Satisfiability::Unsatisfiable;
  // Radix-order indices, preserving the original patterns. Each fixed-size
  // digit pass visits every value, with no comparison-sort or hash
  // collision cost. A repeated canonical value witnesses overlap even when
  // masks differ; distinct values suffice when all masks are equal.
  std::vector<size_t> order(patterns.size()), scratch(patterns.size());
  for (size_t index = 0; index != order.size(); ++index)
    order[index] = index;
  uint64_t bytes = (uint64_t{width} + 7) / 8;
  for (uint64_t byte = 0; byte != bytes; ++byte) {
    auto digit = [&](size_t index) -> unsigned {
      uint64_t word = patterns[index].value.getRawData()[byte / 8];
      return (word >> ((byte % 8) * 8)) & 255;
    };
    std::array<size_t, 256> positions{};
    for (size_t index : order)
      ++positions[digit(index)];
    size_t next = 0;
    for (size_t &position : positions) {
      size_t count = position;
      position = next;
      next += count;
    }
    for (size_t index : order)
      scratch[positions[digit(index)]++] = index;
    order.swap(scratch);
  }
  for (size_t index = 1; index != order.size(); ++index)
    if (patterns[order[index - 1]].value == patterns[order[index]].value)
      return Satisfiability::Satisfiable;
  if (equalMasks)
    return Satisfiability::Unsatisfiable;
#ifdef OBELISK_ENABLE_Z3
  return detail::checkRandomDomainOverlapZ3(patterns, resourceLimit);
#else
  (void)resourceLimit;
  // Preserve small mixed-mask domains in solver-free compiler builds. Charge
  // every full-width comparison so total work stays proportional to input.
  if (patternWords > UINT64_MAX / 4)
    return Satisfiability::Unknown;
  uint64_t remaining = patternWords * 4;
  for (size_t index = 0; index != patterns.size(); ++index) {
    const RandomDomainPattern &pattern = patterns[index];
    for (const RandomDomainPattern &other : patterns.take_front(index)) {
      uint64_t words = pattern.mask.getNumWords();
      if (remaining < words)
        return Satisfiability::Unknown;
      remaining -= words;
      if (((pattern.value ^ other.value) & (pattern.mask & other.mask))
              .isZero())
        return Satisfiability::Satisfiable;
    }
  }
  return Satisfiability::Unsatisfiable;
#endif
}

#ifndef OBELISK_ENABLE_Z3
RandomProgramAnalysis analyzeRandomProgram(const uint8_t *, size_t, uint64_t,
                                           bool) {
  return {};
}
#endif

} // namespace obelisk::solver
