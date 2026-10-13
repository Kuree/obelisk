//===- CoverageCountProjectionIndex.h - bounded relational counts --------===//

#ifndef OBELISK_LIB_COVERAGE_COVERAGECOUNTPROJECTIONINDEX_H
#define OBELISK_LIB_COVERAGE_COVERAGECOUNTPROJECTIONINDEX_H

#include "CoverageDecisionIndex.h"

namespace obelisk::coverage::detail {

/// Count removed-variable assignments separately for each retained assignment.
/// Counts saturate at cap and use shared Boolean graphs for their binary
/// digits. Source nodes, scaled edge results and identical digit vectors are
/// shared. Distinct digit states are charged to the resource budget; thresholds
/// above the physical domain bound require no count-digit allocation.
class DecisionCountProjectionIndex {
public:
  using Magnitude = std::vector<uint64_t>;
  static void normalize(Magnitude &value) {
    while (!value.empty() && !value.back())
      value.pop_back();
  }
  static int compareMagnitudes(const Magnitude &a, const Magnitude &b) {
    if (a.size() != b.size())
      return a.size() < b.size() ? -1 : 1;
    for (size_t limb = a.size(); limb; --limb)
      if (a[limb - 1] != b[limb - 1])
        return a[limb - 1] < b[limb - 1] ? -1 : 1;
    return 0;
  }

private:
  using Root = DecisionIndex::Root;
  using Bits = std::vector<Root>;

  DecisionIndex &source, &destination;
  std::vector<uint32_t> variables, removedBefore;
  Magnitude cap, domainBound;
  uint32_t bitCount = 0;
  size_t limit;
  bool failed = false;
  size_t digitStates = 0;
  // IndexMap stores keys in a deque, so interned vectors retain stable
  // addresses.
  IndexMap<Bits, uint8_t> digits;
  IndexMap<Root, const Bits *> counts;
  IndexMap<std::pair<Root, uint32_t>, const Bits *> scaled;
  IndexMap<std::pair<Root, Magnitude>, Root> selections;

  bool reserveState(size_t digits = 0) {
    if (exhausted() ||
        counts.size() + scaled.size() + selections.size() +
                this->digits.size() + digitStates >=
            limit ||
        digits > limit - counts.size() - scaled.size() - selections.size() -
                     this->digits.size() - digitStates - 1) {
      failed = true;
      return false;
    }
    digitStates += digits;
    return true;
  }

  const Bits *intern(Bits value) {
    auto found = digits.find(value);
    if (found != digits.end())
      return &found->first;
    if (!reserveState(value.size()))
      return nullptr;
    return &digits.emplace(std::move(value), 0).first->first;
  }

  static bool bitAt(const Magnitude &value, uint32_t bit) {
    return bit / 64 < value.size() && ((value[bit / 64] >> (bit % 64)) & 1);
  }

  Root compare(const Bits &bits, const Magnitude &threshold) {
    Root result = 1;
    for (uint32_t bit = 0; bit != bitCount; ++bit)
      result = bitAt(threshold, bit) ? destination.intersect(bits[bit], result)
                                     : destination.unite(bits[bit], result);
    return result;
  }

  Root exclusiveOr(Root left, Root right) {
    return destination.unite(destination.subtract(left, right),
                             destination.subtract(right, left));
  }

  Bits clamp(Bits bits, Root overflow) {
    Root saturated = destination.unite(overflow, compare(bits, cap));
    if (!saturated)
      return bits;
    for (uint32_t bit = 0; bit != bitCount; ++bit) {
      bits[bit] = destination.subtract(bits[bit], saturated);
      if (bitAt(cap, bit))
        bits[bit] = destination.unite(bits[bit], saturated);
    }
    return bits;
  }

  Bits add(const Bits &left, const Bits &right) {
    Bits result(bitCount);
    Root carry = 0;
    for (uint32_t bit = 0; bit != bitCount; ++bit) {
      Root different = exclusiveOr(left[bit], right[bit]);
      result[bit] = exclusiveOr(different, carry);
      carry = destination.unite(destination.intersect(left[bit], right[bit]),
                                destination.intersect(different, carry));
    }
    return clamp(std::move(result), carry);
  }

  const Bits &scale(Root root, uint32_t shift) {
    const Bits &bits = *counts.find(root)->second;
    if (!shift)
      return bits;
    shift = std::min(shift, bitCount);
    auto key = std::make_pair(root, shift);
    auto found = scaled.find(key);
    if (found != scaled.end())
      return *found->second;
    Bits result(bitCount);
    Root overflow = 0;
    for (uint32_t bit = 0; bit != bitCount; ++bit)
      if (bit < bitCount - shift)
        result[bit + shift] = bits[bit];
      else
        overflow = destination.unite(overflow, bits[bit]);
    result = clamp(std::move(result), overflow);
    const Bits *stored = intern(std::move(result));
    if (!stored || !reserveState())
      return bits;
    scaled.emplace(key, stored);
    return *stored;
  }

  bool prepare(Root root) {
    if (counts.empty()) {
      if (limit < 4 || bitCount > (limit - 4) / 2) {
        failed = true;
        return false;
      }
      Bits one(bitCount);
      one[0] = 1;
      const Bits *zero = intern(Bits(bitCount));
      const Bits *unit = intern(std::move(one));
      if (!zero || !unit)
        return false;
      counts.emplace(0, zero);
      counts.emplace(1, unit);
    }
    std::vector<Root> stack{root};
    while (!stack.empty() && !exhausted()) {
      Root value = stack.back();
      if (counts.count(value)) {
        stack.pop_back();
        continue;
      }
      Root zero = source.child(value, false), one = source.child(value, true);
      if (!counts.count(zero)) {
        stack.push_back(zero);
        continue;
      }
      if (!counts.count(one)) {
        stack.push_back(one);
        continue;
      }
      uint32_t variable = source.variable(value);
      const Bits &zeroBits = scale(zero, removedBefore[source.variable(zero)] -
                                             removedBefore[variable + 1]);
      if (exhausted())
        break;
      const Bits &oneBits = scale(one, removedBefore[source.variable(one)] -
                                           removedBefore[variable + 1]);
      if (exhausted())
        break;
      Bits result;
      if (variables[variable] == UINT32_MAX) {
        result = add(zeroBits, oneBits);
      } else {
        result.resize(bitCount);
        for (uint32_t bit = 0; bit != bitCount; ++bit)
          result[bit] = destination.makeNode(variables[variable], zeroBits[bit],
                                             oneBits[bit]);
      }
      const Bits *stored = intern(std::move(result));
      if (!stored || !reserveState())
        break;
      counts.emplace(value, stored);
      stack.pop_back();
    }
    return !exhausted();
  }

public:
  DecisionCountProjectionIndex(const DecisionCountProjectionIndex &) = delete;
  DecisionCountProjectionIndex &
  operator=(const DecisionCountProjectionIndex &) = delete;

  DecisionCountProjectionIndex(DecisionIndex &source,
                               DecisionIndex &destination,
                               std::vector<uint32_t> variables, uint64_t cap,
                               size_t limit = size_t{1} << 18)
      : DecisionCountProjectionIndex(source, destination, std::move(variables),
                                     Magnitude{cap}, limit) {}

  DecisionCountProjectionIndex(DecisionIndex &source,
                               DecisionIndex &destination,
                               std::vector<uint32_t> variables, Magnitude cap,
                               size_t limit = size_t{1} << 18)
      : source(source), destination(destination),
        variables(std::move(variables)), cap(std::move(cap)), limit(limit) {
    normalize(this->cap);
    failed = this->cap.empty() || limit < 2 ||
             this->variables.size() != source.getWidth();
    if (failed)
      return;
    removedBefore.resize(uint64_t{source.getWidth()} + 1);
    uint64_t next = 0;
    for (size_t index = 0; index != this->variables.size(); ++index) {
      uint32_t variable = this->variables[index];
      removedBefore[index + 1] =
          removedBefore[index] + (variable == UINT32_MAX);
      if (variable == UINT32_MAX)
        continue;
      if (variable < next || variable >= destination.getWidth())
        failed = true;
      next = uint64_t{variable} + 1;
    }
    uint32_t removed = removedBefore.back();
    domainBound.resize(uint64_t{removed} / 64 + 1);
    domainBound[removed / 64] = uint64_t{1} << (removed % 64);
    if (compareMagnitudes(this->cap, domainBound) > 0)
      this->cap = domainBound;
    uint64_t highest = this->cap.back();
    uint64_t width = uint64_t{this->cap.size() - 1} * 64;
    while (highest) {
      ++width;
      highest >>= 1;
    }
    if (width > UINT32_MAX) {
      failed = true;
      return;
    }
    bitCount = width;
  }

  bool exhausted() const {
    return failed || source.exhausted() || destination.exhausted();
  }

  /// Return retained assignments with at least threshold source assignments.
  /// Thresholds above the physical domain bound select nothing. Otherwise the
  /// configured cap must be at least threshold; failure returns an empty root
  /// and is reported by exhausted(), rather than publishing partial data.
  Root atLeast(Root root, uint64_t threshold) {
    return atLeast(root, Magnitude{threshold});
  }
  Root atLeast(Root root, Magnitude threshold) {
    normalize(threshold);
    if (exhausted())
      return 0;
    if (compareMagnitudes(threshold, domainBound) > 0)
      return 0;
    if (compareMagnitudes(threshold, cap) > 0)
      failed = true;
    if (exhausted())
      return 0;
    if (threshold.empty())
      return 1;
    if (!root)
      return 0;
    auto key = std::make_pair(root, threshold);
    auto found = selections.find(key);
    if (found != selections.end())
      return found->second;
    if (!prepare(root))
      return 0;
    Root result =
        compare(scale(root, removedBefore[source.variable(root)]), threshold);
    if (!reserveState())
      return 0;
    selections.emplace(key, result);
    return result;
  }
};

} // namespace obelisk::coverage::detail

#endif
