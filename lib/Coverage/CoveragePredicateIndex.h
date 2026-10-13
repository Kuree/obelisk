//===- CoveragePredicateIndex.h - symbolic integral cross predicates -----===//
#ifndef OBELISK_LIB_COVERAGE_COVERAGEPREDICATEINDEX_H
#define OBELISK_LIB_COVERAGE_COVERAGEPREDICATEINDEX_H

#include "CoverageDecisionIndex.h"
#include "obelisk/Coverage/CoverageDatabase.h"

namespace obelisk::coverage::detail {

// Four-state bit vectors use one Boolean graph for each A and B plane bit.
// Every expression node is compiled once. Boolean graph operations retain
// their shared memo; work is bounded by typed bits and generated graph states.
class PredicateDecisionIndex {
public:
  using Root = DecisionIndex::Root;
  struct Value {
    std::vector<Root> aval, bval;
    bool isSigned = false;
  };

private:
  DecisionIndex &graph;
  IndexMap<uint64_t, Value> values;
  bool failed = false;
  Root notRoot(Root value) { return graph.subtract(1, value); }
  Root xorRoot(Root a, Root b) {
    return graph.unite(graph.subtract(a, b), graph.subtract(b, a));
  }
  struct Truth {
    Root one, zero, unknown;
  };
  Truth truth(const Value &value) {
    Root one = 0, zero = 1;
    for (size_t bit = 0; bit != value.aval.size(); ++bit) {
      one = graph.unite(one, graph.subtract(value.aval[bit], value.bval[bit]));
      zero = graph.intersect(
          zero, notRoot(graph.unite(value.aval[bit], value.bval[bit])));
    }
    return {one, zero, notRoot(graph.unite(one, zero))};
  }
  Value predicate(Root one, Root unknown = 0) {
    return {{graph.unite(one, unknown)}, {unknown}, false};
  }
  Root unknown(const Value &left, const Value &right) {
    Root result = 0;
    // Fold corresponding bits together in the graph's interleaved order.
    // Folding all of one operand first would repeatedly rebuild its prefix
    // when inserting the other operand's variables.
    for (size_t bit = 0; bit != left.bval.size(); ++bit)
      result =
          graph.unite(result, graph.unite(left.bval[bit], right.bval[bit]));
    return result;
  }

  Value window(const CrossPredicateNode &node, const Value &source,
               Value ordinal, Root indeterminate) {
    Value result;
    result.aval.assign(node.bitWidth, 1);
    result.bval.assign(node.bitWidth, 1);
    const uint64_t prefix = node.bitWidth - node.elementWidth;
    const uint64_t elements =
        (source.aval.size() + 2 * prefix) / node.elementWidth;
    uint32_t levels = 0;
    for (uint64_t count = elements; count > 1; count = count / 2 + count % 2)
      ++levels;

    // Unknown indices are handled by the final X guard. Their arbitrary
    // numeric bits must not enlarge the addressing graph or its span bounds.
    for (Root &bit : ordinal.aval)
      bit = graph.subtract(bit, indeterminate);
    std::fill(ordinal.bval.begin(), ordinal.bval.end(), 0);

    // Virtual padding converts a partially negative start into a nonnegative
    // address. A base naming the high element already includes this offset.
    // All arithmetic retains the original self-determined index precision.
    if (!(node.flags & CrossPredicateBaseNamesHigh)) {
      const uint64_t padding = prefix / node.elementWidth;
      Value offset;
      offset.aval.assign(ordinal.aval.size(), 0);
      offset.bval = offset.aval;
      for (uint32_t bit = 0; bit != 64; ++bit)
        offset.aval[bit] = (padding >> bit) & 1;
      CrossPredicateNode add;
      add.kind = CrossPredicateKind::Add;
      add.bitWidth = ordinal.aval.size();
      ordinal = evaluate(add, std::move(ordinal), std::move(offset));
    }
    Root invalid = indeterminate;
    for (uint32_t bit = levels; bit != ordinal.aval.size(); ++bit)
      invalid = graph.unite(invalid, ordinal.aval[bit]);
    if (invalid == 1)
      return result;

    using Key = std::pair<uint32_t, uint64_t>;
    using Planes = std::pair<Root, Root>;
    IndexMap<Key, Planes> memo;
    std::vector<uint64_t> minimum(levels + 1), maximum(levels + 1);
    std::vector<uint32_t> active(levels + 1);
    for (uint32_t level = 1; level <= levels; ++level) {
      Root bit = ordinal.aval[level - 1];
      uint64_t step = (uint64_t{1} << (level - 1)) * node.elementWidth;
      minimum[level] = minimum[level - 1] + (bit == 1 ? step : 0);
      maximum[level] = maximum[level - 1] + (bit ? step : 0);
      active[level] = bit > 1 ? level : active[level - 1];
    }
    auto keyFor = [&](uint32_t level, uint64_t position) -> Key {
      uint32_t remaining = active[level];
      return {remaining, position + minimum[level] - minimum[remaining]};
    };
    // Homogeneous source columns need no mux state. This also handles a
    // repeated sign-extension bit and identical packed elements in O(bits).
    std::vector<uint32_t> sameEnd(source.aval.size());
    for (size_t remaining = source.aval.size(); remaining; --remaining) {
      size_t bit = remaining - 1;
      size_t next = bit + uint64_t{node.elementWidth};
      sameEnd[bit] = bit;
      if (next < source.aval.size() && source.aval[bit] == source.aval[next] &&
          source.bval[bit] == source.bval[next])
        sameEnd[bit] = sameEnd[next];
    }
    auto known = [&](Key key, Planes &planes) {
      auto [level, position] = key;
      uint64_t first = position + minimum[level];
      uint64_t last = position + maximum[level];
      if (first >= prefix + source.aval.size() || last < prefix) {
        planes = {1, 1};
        return true;
      }
      if (first >= prefix && last < prefix + source.aval.size() &&
          sameEnd[first - prefix] >= last - prefix) {
        planes = {source.aval[first - prefix], source.bval[first - prefix]};
        return true;
      }
      auto found = memo.find(key);
      if (found == memo.end())
        return false;
      planes = found->second;
      return true;
    };
    struct Frame {
      Key key;
      bool ready = false;
    };
    std::vector<Frame> pending;
    // Adjacent output bits share the same (remaining address bits, source
    // position) states. Each state is compiled once across the entire window;
    // work is linear in typed bits and distinct bounded selector/graph states,
    // rather than an independent source traversal for every output bit.
    for (uint32_t bit = 0; bit != node.bitWidth; ++bit) {
      Key root = keyFor(levels, bit);
      Planes planes;
      pending.push_back({root});
      while (!pending.empty() && !graph.exhausted()) {
        Frame frame = pending.back();
        if (known(frame.key, planes)) {
          pending.pop_back();
          continue;
        }
        auto [level, position] = frame.key;
        const uint64_t step = (uint64_t{1} << (level - 1)) * node.elementWidth;
        Key low = keyFor(level - 1, position);
        Key high = keyFor(level - 1, position + step);
        Root one = ordinal.aval[level - 1];
        if (!frame.ready) {
          pending.back().ready = true;
          if (one != 1 && !known(low, planes))
            pending.push_back({low});
          if (one != 0 && !known(high, planes))
            pending.push_back({high});
          continue;
        }
        Planes left{1, 1}, right{1, 1};
        if ((one != 1 && !known(low, left)) ||
            (one != 0 && !known(high, right)) || !graph.reserveDerivedState()) {
          failed = true;
          return {};
        }
        if (one == 0)
          planes = left;
        else if (one == 1)
          planes = right;
        else {
          Root zero = notRoot(one);
          auto mux = [&](Root a, Root b) {
            return graph.unite(graph.intersect(zero, a),
                               graph.intersect(one, b));
          };
          planes = {mux(left.first, right.first),
                    mux(left.second, right.second)};
        }
        memo.emplace(frame.key, planes);
        pending.pop_back();
      }
      if (graph.exhausted() || !known(root, planes)) {
        failed = true;
        return {};
      }
      result.aval[bit] =
          graph.unite(graph.subtract(planes.first, invalid), invalid);
      result.bval[bit] = graph.unite(planes.second, invalid);
    }
    return result;
  }

  Value select(const CrossPredicateNode &node, const Value &source,
               Value index) {
    Value result;
    result.aval.assign(node.bitWidth, 1);
    result.bval.assign(node.bitWidth, 1);
    if (!node.elementWidth || source.aval.empty() ||
        source.aval.size() % node.elementWidth ||
        node.bitWidth % node.elementWidth ||
        (node.kind == CrossPredicateKind::ElementSelect &&
         node.bitWidth != node.elementWidth) ||
        index.aval.size() > UINT32_MAX - 2) {
      failed = true;
      return {};
    }
    // Normalize the self-determined index without truncating wide values or
    // wrapping negative indices back into the source. The extra sign bits
    // cover both an unsigned index and a signed 64-bit declared bound.
    uint32_t precision = std::max<size_t>(index.aval.size(), 64) + 2;
    resize(index, precision, index.isSigned);
    Value boundary;
    boundary.aval.resize(precision);
    boundary.bval.assign(precision, 0);
    for (uint32_t bit = 0; bit != precision; ++bit)
      boundary.aval[bit] =
          bit < 64 ? (uint64_t(node.indexBase) >> bit) & 1 : node.indexBase < 0;
    CrossPredicateNode subtract;
    subtract.kind = CrossPredicateKind::Subtract;
    subtract.bitWidth = precision;
    Value ordinal =
        node.flags & CrossPredicateAscending
            ? evaluate(subtract, std::move(boundary), std::move(index))
            : evaluate(subtract, std::move(index), std::move(boundary));
    Root indeterminate = 0;
    for (Root bit : ordinal.bval)
      indeterminate = graph.unite(indeterminate, bit);
    if (indeterminate == 1)
      return result;

    bool fixed = indeterminate == 0;
    for (Root bit : ordinal.aval)
      fixed &= bit <= 1;
    if (node.kind == CrossPredicateKind::RangeSelect && !fixed &&
        node.bitWidth != node.elementWidth)
      return window(node, source, std::move(ordinal), indeterminate);
    if (fixed) {
      // Constant indices copy only the selected window. Decode just the
      // magnitude that can overlap the physical source; an arbitrarily wide
      // out-of-range index is all X, without a source copy or large shifts.
      const bool negative = ordinal.aval.back();
      bool carry = negative, tooWide = false;
      uint64_t magnitude = 0;
      for (uint32_t bit = 0; bit != precision; ++bit) {
        bool input = bool(ordinal.aval[bit]) ^ negative;
        bool digit = input ^ carry;
        carry &= input;
        if (digit) {
          if (bit >= 64)
            tooWide = true;
          else
            magnitude |= uint64_t{1} << bit;
        }
      }
      const uint64_t bound =
          negative ? uint64_t{node.bitWidth} / node.elementWidth
                   : (source.aval.size() + uint64_t{node.bitWidth}) /
                             node.elementWidth +
                         1;
      if (tooWide || magnitude > bound)
        return result;
      int64_t offset = magnitude * node.elementWidth;
      if (negative)
        offset = -offset;
      if (node.flags & CrossPredicateBaseNamesHigh)
        offset -= node.bitWidth - node.elementWidth;
      for (uint32_t bit = 0; bit != node.bitWidth; ++bit) {
        // Unsigned addition also handles a partially negative window safely.
        uint64_t position = uint64_t(offset) + bit;
        if (position < source.aval.size()) {
          result.aval[bit] = source.aval[position];
          result.bval[bit] = source.bval[position];
        }
      }
      return result;
    }

    uint32_t elements = source.aval.size() / node.elementWidth;
    uint32_t levels = 0;
    for (uint32_t count = elements; count > 1; count = count / 2 + count % 2)
      ++levels;
    Root invalid = indeterminate;
    for (uint32_t bit = levels; bit != precision; ++bit)
      invalid = graph.unite(invalid, ordinal.aval[bit]);
    if (invalid == 1)
      return result;

    // Adjacent elements are paired at every level. Their total physical size
    // shrinks geometrically, so selecting any packed element takes O(source
    // bits + index bits), apart from the memoized Boolean graph operations.
    Value mux = source;
    for (uint32_t level = 0; elements > 1; ++level) {
      Root one = ordinal.aval[level], zero = notRoot(one);
      uint32_t pairs = elements / 2 + elements % 2;
      for (uint32_t pair = 0; pair != pairs; ++pair)
        for (uint32_t bit = 0; bit != node.elementWidth; ++bit) {
          uint32_t output = pair * node.elementWidth + bit;
          uint32_t low = pair * 2 * node.elementWidth + bit;
          uint32_t high = low + node.elementWidth;
          mux.aval[output] = graph.unite(
              graph.intersect(zero, mux.aval[low]),
              graph.intersect(one,
                              pair * 2 + 1 < elements ? mux.aval[high] : 1));
          mux.bval[output] = graph.unite(
              graph.intersect(zero, mux.bval[low]),
              graph.intersect(one,
                              pair * 2 + 1 < elements ? mux.bval[high] : 1));
        }
      elements = pairs;
      mux.aval.resize(uint64_t{elements} * node.elementWidth);
      mux.bval.resize(uint64_t{elements} * node.elementWidth);
    }
    for (uint32_t bit = 0; bit != node.bitWidth; ++bit) {
      result.aval[bit] =
          graph.unite(graph.subtract(mux.aval[bit], invalid), invalid);
      result.bval[bit] = graph.unite(mux.bval[bit], invalid);
    }
    return result;
  }

  Value evaluate(const CrossPredicateNode &node, Value left, Value right) {
    using Kind = CrossPredicateKind;
    if (node.kind == Kind::Cast) {
      resize(left, node.bitWidth, left.isSigned);
      return left;
    }
    if (node.kind == Kind::LogicalNot) {
      Truth input = truth(left);
      return predicate(input.zero, input.unknown);
    }
    if (node.kind == Kind::Negate) {
      Value zero;
      zero.aval.assign(node.bitWidth, 0);
      zero.bval = zero.aval;
      zero.isSigned = left.isSigned;
      CrossPredicateNode subtract = node;
      subtract.kind = Kind::Subtract;
      return evaluate(subtract, std::move(zero), std::move(left));
    }
    if (node.kind >= Kind::ReductionAnd && node.kind <= Kind::ReductionXnor) {
      const bool conjunction =
          node.kind == Kind::ReductionAnd || node.kind == Kind::ReductionNand;
      const bool exclusive =
          node.kind == Kind::ReductionXor || node.kind == Kind::ReductionXnor;
      const bool inverted = node.kind == Kind::ReductionNand ||
                            node.kind == Kind::ReductionNor ||
                            node.kind == Kind::ReductionXnor;
      Root one = conjunction ? 1 : 0, zero = conjunction ? 0 : 1;
      Root parity = 0, indeterminate = 0;
      // The operand is self-determined, even though its result is one bit.
      // Fold each A/B pair once in the graph's interleaved variable order.
      for (size_t bit = 0; bit != left.aval.size(); ++bit) {
        Root a = left.aval[bit], b = left.bval[bit];
        if (exclusive) {
          parity = xorRoot(parity, a);
          indeterminate = graph.unite(indeterminate, b);
        } else {
          Root knownOne = graph.subtract(a, b);
          Root knownZero = notRoot(graph.unite(a, b));
          one = conjunction ? graph.intersect(one, knownOne)
                            : graph.unite(one, knownOne);
          zero = conjunction ? graph.unite(zero, knownZero)
                             : graph.intersect(zero, knownZero);
        }
      }
      if (exclusive)
        return predicate(inverted ? notRoot(parity) : parity, indeterminate);
      return predicate(inverted ? zero : one, notRoot(graph.unite(one, zero)));
    }
    if (node.kind == Kind::LogicalAnd || node.kind == Kind::LogicalOr) {
      Truth a = truth(left), b = truth(right);
      Root one = node.kind == Kind::LogicalAnd ? graph.intersect(a.one, b.one)
                                               : graph.unite(a.one, b.one);
      Root zero = node.kind == Kind::LogicalAnd
                      ? graph.unite(a.zero, b.zero)
                      : graph.intersect(a.zero, b.zero);
      return predicate(one, notRoot(graph.unite(one, zero)));
    }
    if (node.kind == Kind::BitNot) {
      resize(left, node.bitWidth, left.isSigned);
      for (size_t bit = 0; bit != left.aval.size(); ++bit)
        left.aval[bit] =
            graph.unite(notRoot(graph.unite(left.aval[bit], left.bval[bit])),
                        left.bval[bit]);
      return left;
    }
    const bool signedOperands = left.isSigned && right.isSigned;
    uint32_t width = std::max(left.aval.size(), right.aval.size());
    if (node.kind >= Kind::BitAnd)
      width = node.bitWidth;
    resize(left, width, signedOperands);
    resize(right, width, signedOperands);
    if (node.kind >= Kind::Equal && node.kind <= Kind::GreaterEqual) {
      const bool caseEquality =
          node.kind == Kind::CaseEqual || node.kind == Kind::CaseNotEqual;
      const bool logicalEquality =
          node.kind == Kind::Equal || node.kind == Kind::NotEqual;
      Root different = 0, knownDifferent = 0, caseDifferent = 0, less = 0;
      Root indeterminate = caseEquality ? 0 : unknown(left, right);
      for (uint32_t bit = 0; bit != width; ++bit) {
        Root a = left.aval[bit], b = right.aval[bit];
        Root difference = xorRoot(a, b);
        if (caseEquality) {
          caseDifferent = graph.unite(
              caseDifferent, graph.unite(difference, xorRoot(left.bval[bit],
                                                             right.bval[bit])));
          continue;
        }
        different = graph.unite(different, difference);
        if (logicalEquality) {
          knownDifferent = graph.unite(
              knownDifferent,
              graph.subtract(difference,
                             graph.unite(left.bval[bit], right.bval[bit])));
          continue;
        }
        if (signedOperands && bit + 1 == width) {
          a = notRoot(a);
          b = notRoot(b);
        }
        // More significant bits override the relation of the lower suffix.
        less = graph.unite(graph.intersect(notRoot(a), b),
                           graph.intersect(notRoot(xorRoot(a, b)), less));
      }
      if (caseEquality)
        return predicate(node.kind == Kind::CaseEqual ? notRoot(caseDifferent)
                                                      : caseDifferent);
      if (logicalEquality)
        return predicate(node.kind == Kind::Equal ? notRoot(different)
                                                  : different,
                         graph.subtract(indeterminate, knownDifferent));
      Root one = node.kind == Kind::Less ? less
                 : node.kind == Kind::LessEqual
                     ? graph.unite(less, notRoot(different))
                 : node.kind == Kind::Greater ? graph.subtract(different, less)
                                              : notRoot(less);
      return predicate(one, indeterminate);
    }
    Value result;
    result.aval.resize(width);
    result.bval.resize(width);
    if (node.kind == Kind::Add || node.kind == Kind::Subtract) {
      Root indeterminate = unknown(left, right);
      Root carry = node.kind == Kind::Subtract ? 1 : 0;
      for (uint32_t bit = 0; bit != width; ++bit) {
        Root a = left.aval[bit], b = right.aval[bit];
        if (node.kind == Kind::Subtract)
          b = notRoot(b);
        Root difference = xorRoot(a, b);
        result.aval[bit] = graph.unite(
            graph.subtract(xorRoot(difference, carry), indeterminate),
            indeterminate);
        result.bval[bit] = indeterminate;
        carry = graph.unite(graph.intersect(a, b),
                            graph.intersect(difference, carry));
      }
      return result;
    }
    for (uint32_t bit = 0; bit != width; ++bit) {
      Root a = left.aval[bit], b = right.aval[bit];
      Root ab = left.bval[bit], bb = right.bval[bit];
      if (node.kind == Kind::BitAnd || node.kind == Kind::BitOr) {
        Root ao = graph.subtract(a, ab), bo = graph.subtract(b, bb);
        Root az = notRoot(graph.unite(a, ab)), bz = notRoot(graph.unite(b, bb));
        Root one = node.kind == Kind::BitAnd ? graph.intersect(ao, bo)
                                             : graph.unite(ao, bo);
        Root zero = node.kind == Kind::BitAnd ? graph.unite(az, bz)
                                              : graph.intersect(az, bz);
        result.bval[bit] = notRoot(graph.unite(one, zero));
        result.aval[bit] = graph.unite(one, result.bval[bit]);
      } else if (node.kind == Kind::BitXor || node.kind == Kind::BitXnor) {
        Root difference = xorRoot(a, b);
        result.bval[bit] = graph.unite(ab, bb);
        result.aval[bit] = graph.unite(graph.subtract(node.kind == Kind::BitXor
                                                          ? difference
                                                          : notRoot(difference),
                                                      result.bval[bit]),
                                       result.bval[bit]);
      } else {
        failed = true;
        return {};
      }
    }
    return result;
  }

public:
  explicit PredicateDecisionIndex(DecisionIndex &graph) : graph(graph) {}
  static void resize(Value &value, uint32_t width, bool signExtend) {
    Root a = signExtend && !value.aval.empty() ? value.aval.back() : 0;
    Root b = signExtend && !value.bval.empty() ? value.bval.back() : 0;
    value.aval.resize(width, a);
    value.bval.resize(width, b);
  }
  bool exhausted() const { return failed || graph.exhausted(); }

  template <typename Leaf>
  Root compile(uint64_t root,
               const std::vector<const CrossPredicateNode *> &nodes,
               Leaf leaf) {
    for (const auto *node : nodes) {
      if (values.count(node->id))
        continue;
      Value value;
      if (node->kind == CrossPredicateKind::Target ||
          node->kind == CrossPredicateKind::Value)
        value = leaf(*node);
      else {
        auto a = values.find(node->left), b = values.find(node->right);
        if (a == values.end() || (node->right && b == values.end())) {
          failed = true;
          return 0;
        }
        if (node->kind == CrossPredicateKind::ElementSelect ||
            node->kind == CrossPredicateKind::RangeSelect)
          value = select(*node, a->second, b->second);
        else
          value = evaluate(*node, a->second, node->right ? b->second : Value{});
      }
      if (value.aval.size() != node->bitWidth ||
          value.bval.size() != node->bitWidth || exhausted()) {
        failed = true;
        return 0;
      }
      if (node->flags & CrossPredicateTwoState)
        for (size_t bit = 0; bit != value.aval.size(); ++bit) {
          value.aval[bit] = graph.subtract(value.aval[bit], value.bval[bit]);
          value.bval[bit] = 0;
        }
      value.isSigned = node->signedness == CoverageSignedness::Signed;
      values.emplace(node->id, std::move(value));
    }
    auto found = values.find(root);
    if (found == values.end()) {
      failed = true;
      return 0;
    }
    return truth(found->second).one;
  }
};

} // namespace obelisk::coverage::detail
#endif
