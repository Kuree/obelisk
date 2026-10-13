//===- CoverageDecisionIndex.h - shared symbolic coverage sets -----------===//

#ifndef OBELISK_LIB_COVERAGE_COVERAGEDECISIONINDEX_H
#define OBELISK_LIB_COVERAGE_COVERAGEDECISIONINDEX_H

#include "CoverageIndex.h"

namespace obelisk::coverage::detail {

/// Reduced ordered decision graph over one fixed-width value domain. Nodes
/// and binary operations have constant-size radix keys. Each distinct graph
/// state is processed once; widths do not cause recursive host-stack growth.
/// The work limit bounds symbolic expansion for arbitrary wildcard unions.
class DecisionIndex {
public:
  using Root = uint64_t;

private:
  struct Node {
    uint32_t variable;
    Root zero, one;
  };
  enum class Operation : uint8_t { Union, Intersection, Difference };
  using OperationKey = std::tuple<Operation, Root, Root>;
  uint32_t width;
  size_t limit;
  bool failed = false;
  std::vector<Node> nodes;
  IndexMap<std::tuple<uint32_t, Root, Root>, Root> unique;
  IndexMap<OperationKey, Root> operations;
  IndexMap<std::pair<Root, uint64_t>, uint64_t> counts;
  size_t derivedStates = 0;

  bool reserveState() {
    if (failed ||
        nodes.size() + operations.size() + counts.size() + derivedStates >=
            limit) {
      failed = true;
      return false;
    }
    return true;
  }

  static OperationKey keyFor(Operation operation, Root left, Root right) {
    if (operation != Operation::Difference && right < left)
      std::swap(left, right);
    return {operation, left, right};
  }

  bool known(const OperationKey &key, Root &result) const {
    auto [operation, left, right] = key;
    if (operation == Operation::Union) {
      if (left == 1 || right == 1)
        result = 1;
      else if (!left || left == right)
        result = right;
      else if (!right)
        result = left;
      else
        goto lookup;
    } else if (operation == Operation::Intersection) {
      if (!left || !right)
        result = 0;
      else if (left == 1 || left == right)
        result = right;
      else if (right == 1)
        result = left;
      else
        goto lookup;
    } else {
      if (!left || right == 1 || left == right)
        result = 0;
      else if (!right)
        result = left;
      else
        goto lookup;
    }
    return true;
  lookup:
    auto found = operations.find(key);
    if (found == operations.end())
      return false;
    result = found->second;
    return true;
  }

  Root apply(Operation operation, Root left, Root right) {
    OperationKey root = keyFor(operation, left, right);
    Root result = 0;
    if (failed || known(root, result))
      return result;
    struct Frame {
      OperationKey key;
      uint32_t variable = 0;
      Root zero = 0;
      bool haveZero = false;
    };
    std::vector<Frame> stack{{root}};
    while (!stack.empty() && !failed) {
      Frame &frame = stack.back();
      auto [op, a, b] = frame.key;
      frame.variable = std::min(nodes[a].variable, nodes[b].variable);
      auto child = [&](Root value, bool one) {
        const Node &node = nodes[value];
        return node.variable == frame.variable ? (one ? node.one : node.zero)
                                               : value;
      };
      bool one = frame.haveZero;
      OperationKey next = keyFor(op, child(a, one), child(b, one));
      Root value = 0;
      if (!known(next, value)) {
        if (stack.size() >= limit) {
          failed = true;
          break;
        }
        stack.push_back({next});
        continue;
      }
      if (!one) {
        frame.zero = value;
        frame.haveZero = true;
        continue;
      }
      result = makeNode(frame.variable, frame.zero, value);
      if (!reserveState())
        break;
      operations.emplace(frame.key, result);
      stack.pop_back();
    }
    return failed ? 0 : result;
  }

  static uint64_t scale(uint64_t value, uint32_t shift, uint64_t cap) {
    if (!value)
      return 0;
    if (shift >= 64 || value > (cap >> shift))
      return cap;
    return std::min(cap, value << shift);
  }

public:
  explicit DecisionIndex(uint32_t width, size_t limit = size_t{1} << 18)
      : width(width), limit(limit), nodes{{width, 0, 0}, {width, 1, 1}} {}

  bool exhausted() const { return failed; }
  /// A projection's constant-size memo entries share the graph's work limit.
  /// Charge each distinct derived state once, including states that reduce
  /// to an existing Boolean root and would otherwise escape that limit.
  bool reserveDerivedState() {
    if (!reserveState())
      return false;
    ++derivedStates;
    return true;
  }
  uint32_t getWidth() const { return width; }
  uint32_t variable(Root root) const { return nodes[root].variable; }
  Root child(Root root, bool one) const {
    return one ? nodes[root].one : nodes[root].zero;
  }
  Root makeNode(uint32_t variable, Root zero, Root one) {
    if (failed)
      return 0;
    if (zero == one)
      return zero;
    assert(variable < width && nodes[zero].variable > variable &&
           nodes[one].variable > variable);
    auto key = std::make_tuple(variable, zero, one);
    auto found = unique.find(key);
    if (found != unique.end())
      return found->second;
    if (!reserveState())
      return 0;
    Root root = nodes.size();
    nodes.push_back({variable, zero, one});
    unique.emplace(key, root);
    return root;
  }
  Root unite(Root left, Root right) {
    return apply(Operation::Union, left, right);
  }
  Root intersect(Root left, Root right) {
    return apply(Operation::Intersection, left, right);
  }
  Root subtract(Root left, Root right) {
    return apply(Operation::Difference, left, right);
  }

  Root interval(const std::vector<uint64_t> &low,
                const std::vector<uint64_t> &high) {
    if (high < low)
      return 0;
    Root lower = 1, upper = 1;
    for (uint32_t bit = 0; bit != width && !failed; ++bit) {
      uint32_t variable = width - bit - 1;
      uint64_t mask = uint64_t{1} << (bit % 64);
      size_t limb = low.size() - bit / 64 - 1;
      lower = low[limb] & mask ? makeNode(variable, 0, lower)
                               : makeNode(variable, lower, 1);
      upper = high[limb] & mask ? makeNode(variable, 1, upper)
                                : makeNode(variable, upper, 0);
    }
    return intersect(lower, upper);
  }

  /// Case-equality A values in one fixed B-plane group. Wildcards vary A
  /// only; callers keep wildcard positions out of the B plane.
  Root cube(const std::vector<uint64_t> &value,
            const std::vector<uint64_t> &wildcard) {
    Root root = 1;
    for (uint32_t bit = 0; bit != width && !failed; ++bit) {
      uint64_t mask = uint64_t{1} << (bit % 64);
      if (wildcard[bit / 64] & mask)
        continue;
      root = value[bit / 64] & mask ? makeNode(width - bit - 1, 0, root)
                                    : makeNode(width - bit - 1, root, 0);
    }
    return root;
  }

  uint64_t cardinality(Root root, uint64_t cap) {
    if (!cap || failed)
      return 0;
    auto weight = [&](Root value, uint64_t &result) {
      if (value < 2) {
        result = value;
        return true;
      }
      auto found = counts.find({value, cap});
      if (found == counts.end())
        return false;
      result = found->second;
      return true;
    };
    std::vector<Root> stack{root};
    uint64_t result = 0;
    while (!stack.empty() && !failed) {
      Root value = stack.back();
      if (weight(value, result)) {
        stack.pop_back();
        continue;
      }
      const Node &node = nodes[value];
      uint64_t zero = 0, one = 0;
      if (!weight(node.zero, zero)) {
        stack.push_back(node.zero);
        continue;
      }
      if (!weight(node.one, one)) {
        stack.push_back(node.one);
        continue;
      }
      zero = scale(zero, nodes[node.zero].variable - node.variable - 1, cap);
      one = scale(one, nodes[node.one].variable - node.variable - 1, cap);
      result = one >= cap - zero ? cap : zero + one;
      if (!reserveState())
        break;
      counts.emplace(std::make_pair(value, cap), result);
      stack.pop_back();
    }
    return failed ? 0 : scale(result, nodes[root].variable, cap);
  }

  /// Enumerate a bounded finite set in unsigned order. Words use little-endian
  /// limbs, like cube(). Count before walking free variables so sparse wide
  /// domains cost their graph and output sizes, never their type cardinality.
  /// Failure leaves the caller's output unchanged.
  bool enumerate(Root root, uint32_t maxValues,
                 std::vector<std::vector<uint64_t>> &result) {
    uint64_t count = cardinality(root, uint64_t{maxValues} + 1);
    if (failed || count > maxValues)
      return false;
    std::vector<std::vector<uint64_t>> values;
    values.reserve(count);
    if (count) {
      std::vector<uint64_t> word((uint64_t{width} + 63) / 64);
      struct Frame {
        Root root;
        uint32_t variable;
        uint8_t stage = 0;
      };
      std::vector<Frame> stack{{root, 0}};
      while (!stack.empty()) {
        Frame &frame = stack.back();
        if (!frame.root) {
          stack.pop_back();
          continue;
        }
        if (frame.variable == width) {
          assert(frame.root == 1);
          values.push_back(word);
          stack.pop_back();
          continue;
        }
        uint32_t bit = width - frame.variable - 1;
        uint64_t mask = uint64_t{1} << (bit % 64);
        if (frame.stage == 2) {
          word[bit / 64] &= ~mask;
          stack.pop_back();
          continue;
        }
        bool one = frame.stage++;
        if (one)
          word[bit / 64] |= mask;
        else
          word[bit / 64] &= ~mask;
        Root next = nodes[frame.root].variable == frame.variable
                        ? child(frame.root, one)
                        : frame.root;
        stack.push_back({next, frame.variable + 1});
      }
    }
    result.swap(values);
    return true;
  }
};

/// Existential image of a shared decision graph. A variable map preserves the
/// order of retained variables; UINT32_MAX removes a variable. Each source
/// node is translated once across all roots submitted to this image, including
/// roots shared by overlapping bins. Source and destination may be the same.
class DecisionProjectionIndex {
  DecisionIndex &source, &destination;
  std::vector<uint32_t> variables;
  IndexMap<DecisionIndex::Root, DecisionIndex::Root> translated;
  bool failed = false;

public:
  DecisionProjectionIndex(DecisionIndex &source, DecisionIndex &destination,
                          std::vector<uint32_t> variables)
      : source(source), destination(destination),
        variables(std::move(variables)) {
    failed = this->variables.size() != source.getWidth();
    uint64_t next = 0;
    for (uint32_t variable : this->variables) {
      if (variable == UINT32_MAX)
        continue;
      if (variable < next || variable >= destination.getWidth())
        failed = true;
      next = uint64_t{variable} + 1;
    }
  }

  bool exhausted() const {
    return failed || source.exhausted() || destination.exhausted();
  }

  DecisionIndex::Root project(DecisionIndex::Root root) {
    using Root = DecisionIndex::Root;
    auto known = [&](Root value, Root &result) {
      if (value < 2) {
        result = value;
        return true;
      }
      auto found = translated.find(value);
      if (found == translated.end())
        return false;
      result = found->second;
      return true;
    };
    if (exhausted())
      return 0;
    std::vector<Root> stack{root};
    Root result = 0;
    while (!stack.empty() && !exhausted()) {
      Root value = stack.back();
      if (known(value, result)) {
        stack.pop_back();
        continue;
      }
      Root zero = 0, one = 0;
      if (!known(source.child(value, false), zero)) {
        stack.push_back(source.child(value, false));
        continue;
      }
      if (!known(source.child(value, true), one)) {
        stack.push_back(source.child(value, true));
        continue;
      }
      uint32_t variable = variables[source.variable(value)];
      result = variable == UINT32_MAX
                   ? destination.unite(zero, one)
                   : destination.makeNode(variable, zero, one);
      translated.emplace(value, result);
      stack.pop_back();
    }
    return exhausted() ? 0 : result;
  }
};

} // namespace obelisk::coverage::detail

#endif
