//===- SteadyUnknownAnalysis.h - Storage bits X/Z in steady state -*- C++
//-*-===//
//
// Which bits of each design storage root hold X or Z once the design runs:
// bits that every repeating write stores X/Z into, and bits that keep an
// initial X (IEEE 1800-2023 6.8, Table 6-7) because nothing writes them. A
// field that a design never assigns in the value it copies into a register
// is the typical case.
//
// The fact is the complement of a least fixed point of *may-known* bits: a
// bit may become known when a write may store a known bit into it, and a
// value bit may be known when it may be computed from may-known bits by the
// LRM's X rules (IEEE 1800-2023 11.4): moves (selections, concatenations,
// extensions, aggregates, selects and block arguments) carry their bits,
// bitwise operators work per bit, and an X/Z operand bit makes arithmetic
// and relations X. Any other computation, call result, argument or
// unresolved read may be known. An indexed selection reaches the elements
// its index may select: the values a bounded loop's induction takes, or
// every element. Writes of run-once initialization code (initial and
// root-initializer processes), and the initial value itself, count only for
// bits that no repeating write replaces in full: at a fixed range, in every
// element through fixed writes or a loop visiting each index, or as a whole
// array. Those bits take what the repeating writes store, while an element
// an indexed write misses keeps its initial value. A write whose target
// cannot be attributed makes every root may-known. One DataFlowSolver over
// the design solves both facts: a sparse forward analysis for values, and
// states anchored per storage root and private allocation for writes and
// reads. Packed values and nested unpacked arrays of packed elements up to
// 4096 bits are tracked bit by bit; a larger array, value or root, shares
// one mask for all its packed leaves.
//
// The result is a guess about steady state, not a proof: initialization can
// leave such a bit known, and writes the design does not contain (VPI) can
// change it. Clients must check it at run time before relying on it.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_ANALYSIS_STEADYUNKNOWNANALYSIS_H
#define TALOS_ANALYSIS_STEADYUNKNOWNANALYSIS_H

#include "mlir/Support/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <utility>

namespace talos::sim {
class SimDesignOp;
}

namespace talos::analysis {

class SteadyUnknownAnalysis {
public:
  /// Sorted, disjoint (offset, width) ranges relative to a root.
  using Ranges = llvm::SmallVector<std::pair<uint64_t, uint64_t>, 2>;

  /// Fails only on malformed IR.
  static mlir::FailureOr<SteadyUnknownAnalysis>
  compute(sim::SimDesignOp design);

  /// The bits of root `descriptor` that hold X/Z in steady state.
  llvm::ArrayRef<std::pair<uint64_t, uint64_t>>
  lookup(uint64_t descriptor) const {
    auto found = roots.find(descriptor);
    return found == roots.end()
               ? llvm::ArrayRef<std::pair<uint64_t, uint64_t>>()
               : llvm::ArrayRef(found->second);
  }
  /// Roots with at least one such bit.
  const llvm::DenseMap<uint64_t, Ranges> &getRoots() const { return roots; }
  /// A write could not be attributed, so no bit is known to hold X/Z.
  bool isTop() const { return top; }

private:
  llvm::DenseMap<uint64_t, Ranges> roots;
  bool top = false;
};

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_STEADYUNKNOWNANALYSIS_H
