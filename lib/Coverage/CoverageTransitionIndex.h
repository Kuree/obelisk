//===- CoverageTransitionIndex.h - symbolic transition subtraction --------===//

#ifndef TALOS_LIB_COVERAGE_COVERAGETRANSITIONINDEX_H
#define TALOS_LIB_COVERAGE_COVERAGETRANSITIONINDEX_H

#include "CoverageDecisionIndex.h"
#include "talos/Coverage/CoverageDatabase.h"

namespace talos::coverage::detail {

/// Reachability in an ordinary transition language with finite excluded
/// subsequences removed. Exclusion words share a prefix trie. A reduced
/// decision graph maps each sample to the set of surviving trie prefixes;
/// adding a disjoint label visits only its decision paths, not every earlier
/// partition. Fixed-size radix keys memoize graph operations and transitions.
/// Product states and graph operations are visited once and charged to an
/// explicit work limit. No sampled values or unbounded gap lengths are
/// enumerated, and no host recursion depends on sample width or word length.
class TransitionIndex {
public:
  using Root = DecisionIndex::Root;
  struct Step {
    Root values;
    uint64_t lower, upper;
    TransitionRepetitionKind repetition;
  };

private:
  struct TrieNode {
    bool terminal = false;
    std::vector<std::pair<Root, uint64_t>> edges;
  };
  struct Head {
    uint64_t previous, node;
  };
  struct MapNode {
    uint32_t variable;
    Root zero, one;
    uint64_t head;
  };
  using AddKey = std::tuple<Root, Root, uint64_t>;
  DecisionIndex &values;
  uint32_t width;
  Root domain;
  size_t limit, work = 0;
  bool failed = false, sealed = false;
  std::vector<TrieNode> trie{1};
  // Heads 0 and 1 denote the empty prefix set and an excluded sample.
  // Root children precede other children; each group is in node order, so
  // hash-consed heads are canonical sets.
  std::vector<Head> heads{{0, 0}, {0, 0}};
  std::vector<MapNode> nodes;
  IndexMap<std::pair<uint64_t, Root>, uint64_t> trieEdges;
  IndexMap<std::pair<uint64_t, uint64_t>, uint64_t> extendedHeads;
  IndexMap<uint64_t, Root> leaves;
  IndexMap<std::tuple<uint32_t, Root, Root>, Root> branches;
  IndexMap<AddKey, Root> added;
  IndexMap<uint64_t, Root> maps;
  IndexMap<Root, Root> survivingMaps;
  IndexMap<std::pair<uint64_t, Root>, std::vector<uint64_t>> moves;

  bool consume() {
    if (failed || work == limit) {
      failed = true;
      return false;
    }
    ++work;
    return true;
  }

  Root leaf(uint64_t head) {
    if (auto found = leaves.find(head); found != leaves.end())
      return found->second;
    if (!consume())
      return 1;
    Root result = nodes.size();
    nodes.push_back({width, 0, 0, head});
    leaves.emplace(head, result);
    return result;
  }

  Root branch(uint32_t variable, Root zero, Root one) {
    if (zero == one)
      return zero;
    auto key = std::make_tuple(variable, zero, one);
    if (auto found = branches.find(key); found != branches.end())
      return found->second;
    if (!consume())
      return 1;
    Root result = nodes.size();
    nodes.push_back({variable, zero, one, 0});
    branches.emplace(key, result);
    return result;
  }

  uint64_t extend(uint64_t head, uint64_t node) {
    auto key = std::make_pair(head, node);
    if (auto found = extendedHeads.find(key); found != extendedHeads.end())
      return found->second;
    if (!consume())
      return 1;
    uint64_t result = heads.size();
    heads.push_back({head, node});
    extendedHeads.emplace(key, result);
    return result;
  }

  bool known(const AddKey &key, Root &result) {
    auto [map, condition, node] = key;
    if (map == 1 || !condition) {
      result = map;
      return true;
    }
    if (condition == 1 && nodes[map].variable == width) {
      result = trie[node].terminal ? 1 : leaf(extend(nodes[map].head, node));
      return true;
    }
    if (auto found = added.find(key); found != added.end()) {
      result = found->second;
      return true;
    }
    return false;
  }

  Root add(Root map, Root condition, uint64_t node) {
    AddKey initial{map, condition, node};
    std::vector<AddKey> pending{initial};
    while (!pending.empty() && !exhausted()) {
      AddKey key = pending.back();
      Root result;
      if (known(key, result)) {
        pending.pop_back();
        continue;
      }
      auto [current, label, child] = key;
      uint32_t variable =
          std::min(nodes[current].variable, values.variable(label));
      auto next = [&, &current = current, &label = label,
                   &child = child](bool one) {
        Root nextMap = nodes[current].variable == variable
                           ? (one ? nodes[current].one : nodes[current].zero)
                           : current;
        Root nextLabel = values.variable(label) == variable
                             ? values.child(label, one)
                             : label;
        return AddKey{nextMap, nextLabel, child};
      };
      Root zero, one;
      AddKey zeroKey = next(false), oneKey = next(true);
      if (!known(zeroKey, zero)) {
        pending.push_back(zeroKey);
        continue;
      }
      if (!known(oneKey, one)) {
        pending.push_back(oneKey);
        continue;
      }
      if (!consume())
        break;
      added.emplace(key, branch(variable, zero, one));
      pending.pop_back();
    }
    Root result = 1;
    if (!exhausted())
      known(initial, result);
    return result;
  }

  Root mapFor(uint64_t head) {
    if (auto found = maps.find(head); found != maps.end())
      return found->second;
    // New exclusion words start on every sample. Compile that shared map
    // once instead of rescanning all root edges for every active prefix set.
    Root result = head ? mapFor(0) : 0;
    if (exhausted())
      return 1;
    std::vector<std::pair<Root, uint64_t>> edges;
    if (!head)
      edges = trie.front().edges;
    for (uint64_t current = head; current >= 2;
         current = heads[current].previous) {
      const auto &nodeEdges = trie[heads[current].node].edges;
      edges.insert(edges.end(), nodeEdges.begin(), nodeEdges.end());
    }
    sortByKey(edges.begin(), edges.end(),
              [](const auto &edge) { return edge.second; });
    for (auto [label, node] : edges) {
      if (!consume())
        return 1;
      result = add(result, label, node);
      if (exhausted())
        return 1;
    }
    maps.emplace(head, result);
    return result;
  }

  const std::vector<uint64_t> &move(uint64_t head, Root condition) {
    auto key = std::make_pair(head, condition);
    if (auto found = moves.find(key); found != moves.end())
      return found->second;
    std::vector<uint64_t> result;
    IndexSet<uint64_t> destinations;
    IndexSet<std::pair<Root, Root>> visited;
    std::vector<std::pair<Root, Root>> pending{{mapFor(head), condition}};
    while (!pending.empty() && !exhausted()) {
      auto [map, label] = pending.back();
      pending.pop_back();
      if (map == 1 || !label || !visited.insert({map, label}).second)
        continue;
      if (!consume())
        break;
      if (nodes[map].variable == width) {
        if (destinations.insert(nodes[map].head).second)
          result.push_back(nodes[map].head);
        continue;
      }
      uint32_t variable = std::min(nodes[map].variable, values.variable(label));
      for (bool one : {false, true})
        pending.emplace_back(nodes[map].variable == variable
                                 ? (one ? nodes[map].one : nodes[map].zero)
                                 : map,
                             values.variable(label) == variable
                                 ? values.child(label, one)
                                 : label);
    }
    return moves.emplace(key, std::move(result)).first->second;
  }

  Root surviving(Root map) {
    std::vector<Root> pending{map};
    while (!pending.empty() && !exhausted()) {
      Root current = pending.back();
      if (survivingMaps.count(current)) {
        pending.pop_back();
        continue;
      }
      const auto &node = nodes[current];
      Root result;
      if (node.variable == width)
        result = node.head == 1 ? 0 : 1;
      else {
        auto zero = survivingMaps.find(node.zero);
        if (zero == survivingMaps.end()) {
          pending.push_back(node.zero);
          continue;
        }
        auto one = survivingMaps.find(node.one);
        if (one == survivingMaps.end()) {
          pending.push_back(node.one);
          continue;
        }
        result = values.makeNode(node.variable, zero->second, one->second);
      }
      if (!consume())
        break;
      survivingMaps.emplace(current, result);
      pending.pop_back();
    }
    auto found = survivingMaps.find(map);
    return exhausted() || found == survivingMaps.end() ? 0 : found->second;
  }

public:
  TransitionIndex(DecisionIndex &values, uint32_t width, Root domain,
                  size_t limit = size_t{1} << 18)
      : values(values), width(width), domain(domain), limit(limit),
        nodes{{width, 0, 0, 0}, {width, 0, 0, 1}} {
    leaves.emplace(0, 0);
    leaves.emplace(1, 1);
  }

  bool exhausted() const { return failed || values.exhausted(); }

  void addExclusion(const std::vector<Root> &word) {
    if (sealed || word.empty()) {
      failed = true;
      return;
    }
    if (std::find(word.begin(), word.end(), Root{0}) != word.end())
      return;
    uint64_t current = 0;
    for (Root label : word) {
      if (!consume())
        return;
      auto key = std::make_pair(current, label);
      auto found = trieEdges.find(key);
      if (found != trieEdges.end()) {
        current = found->second;
        continue;
      }
      uint64_t next = trie.size();
      trie.emplace_back();
      trie[current].edges.emplace_back(label, next);
      trieEdges.emplace(key, next);
      current = next;
    }
    trie[current].terminal = true;
  }

private:
  Root accepted(const std::vector<Step> &steps, bool stopAtFirst) {
    sealed = true;
    if (steps.empty() ||
        std::any_of(steps.begin(), steps.end(),
                    [](const Step &step) { return !step.values; }))
      return 0;
    Root terminals = 0;
    // ordinal, repetition count, nonconsecutive gap constraint, trie prefixes.
    using State = std::tuple<uint32_t, uint64_t, uint32_t, uint64_t>;
    IndexSet<State> visited;
    std::vector<State> pending{{0, 0, UINT32_MAX, 0}};
    auto retain = [&](State state) {
      if (visited.insert(state).second) {
        if (consume())
          pending.push_back(state);
      }
    };
    visited.insert(pending.front());
    while (!pending.empty() && !exhausted()) {
      auto [ordinal, consumed, forbidden, head] = pending.back();
      pending.pop_back();
      const Step &step = steps[ordinal];
      uint64_t count = consumed + 1;
      const auto &matched = move(head, step.values);
      if (ordinal + 1 == steps.size() && count >= step.lower &&
          !matched.empty()) {
        if (stopAtFirst)
          return exhausted() ? 0 : 1;
        Root available = values.intersect(step.values, surviving(mapFor(head)));
        terminals = values.unite(terminals, available);
        // No accepted terminal can lie outside the authored final step.
        if (terminals == steps.back().values)
          return exhausted() ? 0 : terminals;
      }
      for (uint64_t next : matched) {
        if (count >= step.lower && ordinal + 1 < steps.size()) {
          retain({ordinal + 1, 0,
                  step.repetition == TransitionRepetitionKind::Nonconsecutive
                      ? ordinal
                      : UINT32_MAX,
                  next});
        }
        if (count < step.upper)
          retain({ordinal, count, UINT32_MAX, next});
      }
      Root gaps = 0;
      if (forbidden != UINT32_MAX)
        gaps = values.subtract(domain, steps[forbidden].values);
      else if (step.repetition == TransitionRepetitionKind::Goto ||
               step.repetition == TransitionRepetitionKind::Nonconsecutive)
        gaps = values.subtract(domain, step.values);
      if (gaps)
        for (uint64_t next : move(head, gaps))
          retain({ordinal, consumed, forbidden, next});
    }
    return exhausted() ? 0 : terminals;
  }

public:
  bool hasAcceptedWord(const std::vector<Step> &steps) {
    return accepted(steps, true) != 0;
  }

  /// Values of the final authored step that complete at least one clean
  /// sequence. This is the associated value set used by cross selectors.
  Root terminalValues(const std::vector<Step> &steps) {
    return accepted(steps, false);
  }
};

} // namespace talos::coverage::detail

#endif // TALOS_LIB_COVERAGE_COVERAGETRANSITIONINDEX_H
