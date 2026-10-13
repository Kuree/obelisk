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
